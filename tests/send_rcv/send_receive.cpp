#include "send_receive.hpp"

#include <assert.h>

#include <bits/stdint-uintn.h>
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

#include "error.hpp"
#include "endpoint.hpp"
#include "conn.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"


namespace kym {
namespace connection {

endpoint::Options defaultOptions = {
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

StatusOr<std::unique_ptr<SendReceiveConnection>> DialSendReceive(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto epStatus = kym::endpoint::Dial(ip, port, defaultOptions);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();
  auto conn = std::make_unique<SendReceiveConnection>(ep);
  return StatusOr<std::unique_ptr<SendReceiveConnection>>(std::move(conn));
}

/* 
 * Server listener
 */
StatusOr<std::unique_ptr<SendReceiveListener>> ListenSendReceive(std::string ip, int port) {
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  std::unique_ptr<endpoint::Listener> ln = lnStatus.value();
  auto rcvLn = std::make_unique<SendReceiveListener>(std::move(ln));
  return StatusOr<std::unique_ptr<SendReceiveListener>>(std::move(rcvLn));
}

StatusOr<std::unique_ptr<SendReceiveConnection>> SendReceiveListener::Accept(){
  auto epStatus = this->listener_->Accept(defaultOptions);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();

  auto conn = std::make_unique<SendReceiveConnection>(ep);
  return StatusOr<std::unique_ptr<SendReceiveConnection>>(std::move(conn));

}

SendReceiveListener::SendReceiveListener(std::unique_ptr<endpoint::Listener> listener) : listener_(std::move(listener)) {}


Status SendReceiveListener::Close() {
  return this->listener_->Close();
}
/*
 * SendReceiveConnection
 */
SendReceiveConnection::SendReceiveConnection(std::shared_ptr<endpoint::Endpoint> ep){
  this->allocator_ = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  this->ep_ = ep;

  // TODO(fischi) Parameterize
  size_t transfer_size = 1048576;
  ibv_pd pd = this->ep_->GetPd();
  for (int i = 0; i < 10; i++){

    // TODO(Fischi) Maybe we should replace that was a call to an allocator?
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    // TODO(Fischi) Error handling. This should not be in the constructor
    auto regStatus = this->ep_->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    assert(regStatus.ok());
    this->mrs_.push_back(mr);
  }
}

Status SendReceiveConnection::Close() {
  for (auto mr : this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      std::cerr << "Error " << ret << std::endl;
    }
    free(mr->addr);
  }
  return this->ep_->Close();
}

StatusOr<SendRegion> SendReceiveConnection::GetMemoryRegion(size_t size){
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
Status SendReceiveConnection::Send(SendRegion region){
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
Status SendReceiveConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

StatusOr<ReceiveRegion> SendReceiveConnection::Receive(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }

  struct ibv_wc wc = wcStatus.value();
  ReceiveRegion reg;
  reg.context = wc.wr_id;
  reg.addr = this->mrs_[wc.wr_id]->addr;
  reg.length = wc.byte_len;
  return reg;
}

Status SendReceiveConnection::Free(ReceiveRegion region){
  struct ibv_mr *mr = this->mrs_[region.context];
  return this->ep_->PostRecv(region.context, mr->lkey, mr->addr, mr->length); 
}
}
}
