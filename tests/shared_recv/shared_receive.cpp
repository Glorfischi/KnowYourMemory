#include "shared_receive.hpp"

#include <assert.h>

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>
#include <vector>

#include "endpoint.hpp"
#include "conn.hpp"
#include "error.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"


namespace kym {
namespace connection {

endpoint::Options default_shared_recv_opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 1,
      .max_recv_wr = 10,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 0,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 0,
  .initiator_depth =  0,
  .retry_count = 8,  
  .rnr_retry_count = 8, 
};


 
/*
 * Client Dial
 */

StatusOr<std::unique_ptr<SharedReceiveConnection>> DialSharedReceive(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  auto epStatus = kym::endpoint::Dial(ip, port, default_shared_recv_opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();

  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  // TODO(fischi): enable SRQ sharing for dialing
  auto srq = std::make_shared<SharedReceiveQueue>(ep->GetPd());
  auto conn = std::make_unique<SharedReceiveConnection>(ep, srq);
  return StatusOr<std::unique_ptr<SharedReceiveConnection>>(std::move(conn));
}

/* 
 * Server listener
 */

StatusOr<std::unique_ptr<SharedReceiveListener>> ListenSharedReceive(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  std::unique_ptr<endpoint::Listener> ln = lnStatus.value();
  auto srq = std::make_shared<SharedReceiveQueue>(ln->GetPd());
  auto shLn = std::make_unique<SharedReceiveListener>(std::move(ln), srq);
  return StatusOr<std::unique_ptr<SharedReceiveListener>>(std::move(shLn));
}

StatusOr<std::unique_ptr<SharedReceiveConnection>> SharedReceiveListener::Accept() {
  endpoint::Options opts = default_shared_recv_opts;
  opts.qp_attr.srq = this->srq_->srq_;
  auto epStatus = this->listener_->Accept(opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();

  auto conn = std::make_unique<SharedReceiveConnection>(ep, this->srq_);
  return StatusOr<std::unique_ptr<SharedReceiveConnection>>(std::move(conn));

}

SharedReceiveListener::SharedReceiveListener(std::unique_ptr<endpoint::Listener> ln, 
    std::shared_ptr<SharedReceiveQueue> srq) : listener_(std::move(ln)), srq_(srq) {}


Status SharedReceiveListener::Close() {
  return this->listener_->Close();
}


/*
 * SharedReceiveConnection
 */
SharedReceiveConnection::SharedReceiveConnection(std::shared_ptr<endpoint::Endpoint> ep,
    std::shared_ptr<SharedReceiveQueue> srq): ep_(ep), srq_(srq){
  this->allocator_ = std::make_shared<memory::DumbAllocator>(ep->GetPd());

}
Status SharedReceiveConnection::Close() {
  auto srq_s = this->srq_->Close();
  if (!srq_s.ok()){
    return srq_s;
  }
  return this->ep_->Close();
}

StatusOr<SendRegion> SharedReceiveConnection::GetMemoryRegion(size_t size){
  auto mrStatus =  this->allocator_->Alloc(size);
  if (!mrStatus.ok()){
    return mrStatus.status();
  }
  auto mr = mrStatus.value();
  struct SendRegion reg = {0};
  reg.context = mr.context;
  reg.addr = mr.addr;
  reg.length = mr.length;
  reg.lkey = mr.lkey;
  return reg;
}
Status SharedReceiveConnection::Send(SendRegion region){
  auto sendStatus = this->ep_->PostSend(0, region.lkey, region.addr, region.length);
  if (!sendStatus.ok()){
    return sendStatus;
  }
  auto wcStatus = this->ep_->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  return Status();
}
Status SharedReceiveConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

StatusOr<ReceiveRegion> SharedReceiveConnection::Receive(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }

  struct ibv_wc wc = wcStatus.value();
  auto regStatus = this->srq_->GetRegionById(wc.wr_id);
  if (!regStatus.ok()){
    return regStatus.status();
  }
  ReceiveRegion reg = regStatus.value();
  reg.length = wc.byte_len;
  return reg;
}
Status SharedReceiveConnection::Free(ReceiveRegion region){
  return this->srq_->PostReceiveRegion(region);
}


SharedReceiveQueue::SharedReceiveQueue(struct ibv_pd pd){
  // TODO(fischi) Parameterize
  int posted_buffers = 10;
  size_t transfer_size = 1048576;

  struct ibv_srq_init_attr srq_init_attr;
  srq_init_attr.attr.max_sge = 1;
  srq_init_attr.attr.max_wr = posted_buffers;
  this->srq_ = ibv_create_srq(&pd, &srq_init_attr);

  for (int i = 0; i < posted_buffers; i++){
    // TODO(Fischi) Maybe we should replace that was a call to an allocator?
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    struct ibv_sge sge;
    sge.addr = (uintptr_t)mr->addr;
    sge.length = transfer_size;
    sge.lkey = mr->lkey;

    struct ibv_recv_wr wr, *bad;
    wr.wr_id = i;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    int ret = ibv_post_srq_recv(this->srq_, &wr, &bad);
    if (ret) {
      // TODO(Fischi) Error handling. This should not be in the constructor
      std::cerr << "Error " << ret << std::endl;
    }
    this->mrs_.push_back(mr);
  }

}

Status SharedReceiveQueue::Close() {
  int ret = ibv_destroy_srq(this->srq_);
  if (ret) {
    std::cerr << "Error " << ret << std::endl;
  }
  for (auto mr : this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      return Status(StatusCode::Unknown, "error freeing receive buffer");
    }
    free(mr->addr);
  }
  return Status();
}

Status SharedReceiveQueue::PostReceiveRegion(ReceiveRegion reg){
  //TODO(fischi) not thread safe
  struct ibv_mr *mr = this->mrs_[reg.context];

  struct ibv_sge sge;
  sge.addr = (uintptr_t)mr->addr;
  sge.length = mr->length;
  sge.lkey = mr->lkey;

  struct ibv_recv_wr wr, *bad;
  wr.wr_id = reg.context;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
 
  int ret = ibv_post_srq_recv(this->srq_, &wr, &bad);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error posting to srq");
  }
  return Status();
}

StatusOr<ReceiveRegion> SharedReceiveQueue::GetRegionById(uint64_t id){
  ReceiveRegion reg = {0};
  reg.context = id;
  reg.addr = this->mrs_[id]->addr;
  reg.length = this->mrs_[id]->length;
  return reg;
}

}
}
