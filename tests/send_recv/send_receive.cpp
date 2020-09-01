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
#include <vector>

#include "error.hpp"
#include "endpoint.hpp"
#include "conn.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"
#include "receive_queue.hpp"


namespace kym {
namespace connection {
namespace {

const uint64_t inflight = 80;

endpoint::Options defaultOptions = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 5*inflight,
      .max_recv_wr = 5*inflight,
      .max_send_sge = 5,
      .max_recv_sge = 5,
      .max_inline_data = 0,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 0,
  .initiator_depth =  0,
  .retry_count = 0,  
  .rnr_retry_count = 0, 
};


}

/*
 * Client Dial
 */

StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port){
  return DialSendReceive(ip, port, NULL);
}

StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, std::string src){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto opts = defaultOptions;
  if (!src.empty()){
    opts.src = src.c_str();
  }
  auto epStatus = kym::endpoint::Dial(ip, port, opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();

  auto allocator = new memory::DumbAllocator(ep->GetPd());

  //TODO(Fischi) parameterize
  auto rq_stat = endpoint::GetReceiveQueue(ep, 8*1024, inflight);
  if (!rq_stat.ok()){
    return rq_stat.status().Wrap("error creating receive queue while dialing");
  }
  endpoint::ReceiveQueue *rq = rq_stat.value();

  auto conn = new SendReceiveConnection(ep, rq, false, allocator);

  return conn;
}

StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, endpoint::IReceiveQueue * rq){
  return DialSendReceive(ip, port, NULL, rq);
}
StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, std::string src, endpoint::IReceiveQueue * rq){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto opts = defaultOptions;
  if (!src.empty()){
    opts.src = src.c_str();
  }
  auto epStatus = kym::endpoint::Dial(ip, port, opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();

  auto allocator = new memory::DumbAllocator(ep->GetPd());

  auto conn = new SendReceiveConnection(ep, rq, true, allocator);
  return conn;
}

/* 
 * Server listener
 */
StatusOr<SendReceiveListener *> ListenSendReceive(std::string ip, int port) {
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  endpoint::Listener *ln = lnStatus.value();
  auto rcvLn = new SendReceiveListener(ln);
  return rcvLn;
}

StatusOr<SendReceiveListener *> ListenSharedReceive(std::string ip, int port) {
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  endpoint::Listener *ln = lnStatus.value();

  //TODO(Fischi) parameterize
  auto srq_stat = endpoint::GetSharedReceiveQueue(ln->GetPd(), 8*1024, inflight);
  if (!srq_stat.ok()){
    return srq_stat.status().Wrap("error creating shared receive queue");
  }
  endpoint::SharedReceiveQueue *srq = srq_stat.value();

  auto rcvLn = new SendReceiveListener(ln, srq);
  return rcvLn;
}

StatusOr<SendReceiveConnection *> SendReceiveListener::Accept(){
  kym::endpoint::Options opts = defaultOptions;
  if (this->srq_ != nullptr){
    opts.qp_attr.srq = this->srq_->GetSRQ();
  }
  auto epStatus = this->listener_->Accept(opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();

  auto allocator = new memory::DumbAllocator(ep->GetPd());

  endpoint::IReceiveQueue *rq;
  if (this->srq_ == nullptr){
    auto rq_stat = endpoint::GetReceiveQueue(ep, 8*1024, inflight);
    if (!rq_stat.ok()){
      return rq_stat.status().Wrap("error creating receive queue while dialing");
    }
    rq = rq_stat.value();
  } else {
    rq = this->srq_;
  }

  auto conn = new SendReceiveConnection(ep, rq, this->srq_ != nullptr, allocator);

  return conn;

}

SendReceiveListener::SendReceiveListener(endpoint::Listener *listener) : listener_(listener) {}
SendReceiveListener::SendReceiveListener(endpoint::Listener *listener, 
    endpoint::SharedReceiveQueue *srq ) : listener_(listener), srq_(srq) {}

SendReceiveListener::~SendReceiveListener(){
  delete this->listener_;
  delete this->srq_;
}
Status SendReceiveListener::Close() {
  if (this->srq_ != nullptr){
    this->srq_->Close();
  }
  return this->listener_->Close();
}
/*
 *SendReceiveConnection
 */
SendReceiveConnection::SendReceiveConnection(endpoint::Endpoint *ep, 
    endpoint::IReceiveQueue *rq, bool rq_shared, memory::Allocator *allocator){
  this->allocator_ = allocator;
  this->ep_ = ep;
  this->rq_ = rq;
  this->rq_shared_ = rq_shared;
}

SendReceiveConnection::~SendReceiveConnection(){
  delete this->allocator_;
  delete this->ep_;
  if (!this->rq_shared_){
    delete this->rq_;
  }
}
Status SendReceiveConnection::Close() {
  if (!this->rq_shared_){
    auto stat = this->rq_->Close();
    if (!stat.ok()){
      return stat.Wrap("error closing receive queue for SendReceiveConnection");
    }
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
Status SendReceiveConnection::Send(std::vector<SendRegion> regions){
  int i = 0;
  int batch_size = regions.size();
  ibv_send_wr wrs[batch_size];
  ibv_sge sges[batch_size];

  for ( auto region : regions){
    struct ibv_sge *sge = &sges[i];
    ibv_send_wr *wr = &wrs[i];
    bool last = (++i == batch_size);

    sge->addr = (uintptr_t)region.addr;
    sge->length = region.length;
    sge->lkey =  region.lkey;

    wr->wr_id = i+100;
    wr->next = last ? NULL : &(wrs[i]);
    wr->sg_list = sge;
    wr->num_sge = 1;
    wr->opcode = IBV_WR_SEND;

    wr->send_flags = last ? IBV_SEND_SIGNALED : 0;  
  }

  struct ibv_send_wr *bad;
  auto sendStatus = this->ep_->PostSendRaw(&(wrs[0]), &bad);
  if (!sendStatus.ok()){
      return sendStatus;
  }

  auto wcStatus = this->ep_->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  ibv_wc wc = wcStatus.value();
  if (wc.wr_id != i+100){
    return Status(StatusCode::Internal, "posible interleafing while sending a batch");
  }
  return Status();
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
    return wcStatus.status().Wrap("error while polling recieve queue for receive");
  }

  struct ibv_wc wc = wcStatus.value();

  struct endpoint::mr mr = this->rq_->GetMR(wc.wr_id);
  ReceiveRegion reg;
  reg.context = wc.wr_id;
  reg.addr = mr.addr;
  reg.length = wc.byte_len;
  return reg;
}

Status SendReceiveConnection::Free(ReceiveRegion region){
  return this->rq_->PostMR(region.context);
}
}
}
