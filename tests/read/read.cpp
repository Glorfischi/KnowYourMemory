#include "read.hpp"

#include <assert.h>

#include <cstdint>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>

#include <thread>
#include <chrono>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "conn.hpp"
#include "error.hpp"
#include "mm.hpp"
#include "endpoint.hpp"

#include "mm/dumb_allocator.hpp"
#include "receive_queue.hpp"


namespace kym {
namespace connection {

uint32_t inflight = 200;
endpoint::Options read_connection_default_opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = inflight,
      .max_recv_wr = inflight,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = sizeof(ReadRequest),
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 16,
  .initiator_depth =  16,
  .retry_count = 1,  
  .rnr_retry_count = 0, 
};


const char one = 1;

/*
 * Client Dial
 */

StatusOr<ReadConnection*> DialRead(std::string ip, int port){
  return DialRead(ip, port, false);
}
StatusOr<ReadConnection*> DialRead(std::string ip, int port, bool fence){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto ep_status = kym::endpoint::Dial(ip, port, read_connection_default_opts);
  if (!ep_status.ok()){
    return ep_status.status();
  }
  endpoint::Endpoint *ep = ep_status.value();


  memory::DumbAllocator *allocator = new memory::DumbAllocator(ep->GetPd());
  auto rq_status = endpoint::GetReceiveQueue(ep, sizeof(ReadRequest), inflight);
  if (!rq_status.ok()){
    return rq_status.status();
  }
  return new ReadConnection(ep, allocator, rq_status.value(), fence);
}

/* 
 * Server listener
 */
StatusOr<ReadListener*> ListenRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  endpoint::Listener *ln = lnStatus.value();
  return new ReadListener(ln);
}

StatusOr<ReadConnection*> ReadListener::Accept(){
  return this->Accept(false);
}
StatusOr<ReadConnection*> ReadListener::Accept(bool fence){
  auto epStatus = this->listener_->Accept(read_connection_default_opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();
  memory::DumbAllocator *allocator = new memory::DumbAllocator(ep->GetPd());
  auto rq_status = endpoint::GetReceiveQueue(ep, sizeof(ReadRequest), inflight);
  if (!rq_status.ok()){
    return rq_status.status();
  }
  return new ReadConnection(ep, allocator, rq_status.value(), fence);

}

Status ReadListener::Close() {
  return this->listener_->Close();
}
ReadListener::~ReadListener() {
  free(this->listener_);
}



/*
 * ReadConnection
 */

ReadConnection::ReadConnection(endpoint::Endpoint *ep, memory::Allocator *allocator, endpoint::IReceiveQueue *rq, bool fence)
  : ep_(ep), allocator_(allocator), rq_(rq), fence_(fence), ackd_rcv_id_(0), next_rcv_id_(1){
  this->outstanding_read_sge_.reserve(inflight);
  this->outstanding_read_wr_.reserve(inflight);
}
Status ReadConnection::Close(){
  auto stat = this->rq_->Close();
  if (!stat.ok()){
    return stat;
  }
  return this->ep_->Close();
}
ReadConnection::~ReadConnection(){
  delete this->rq_;
  delete this->allocator_;
  delete this->ep_;
}

StatusOr<SendRegion> ReadConnection::GetMemoryRegion(size_t size){
  auto mrStatus =  this->allocator_->Alloc(size+1);
  if (!mrStatus.ok()){
    return mrStatus.status();
  }
  auto mr = mrStatus.value();
  struct SendRegion reg = {0};
  reg.context = mr.context;
  reg.addr = (void *)((size_t)mr.addr+1);
  reg.length = size;
  reg.lkey = mr.lkey;
  
  // Make sure the first byte is zeroed
  *(char *)mr.addr = 0;
  return reg;
}

Status ReadConnection::Send(SendRegion region){
  auto id_stat = this->SendAsync(region);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value());
}
StatusOr<uint64_t> ReadConnection::SendAsync(SendRegion region){
  struct ReadRequest req;
  req.addr = (uint64_t)region.addr;
  req.key = region.lkey;
  req.length = region.length;

  StatusOr<ibv_wc> wc_s = Status();
  do {
    wc_s = this->ep_->PollSendCqOnce();
  } while (wc_s.ok());
  if (wc_s.status().code() != StatusCode::NotFound) {
    return wc_s.status().Wrap("error polling send cq while sending");
  }
  
  auto sendStatus = this->ep_->PostInline(0, &req, sizeof(req));
  if (!sendStatus.ok()){
    return sendStatus;
  }
  
  return req.addr-1;
}
Status ReadConnection::Wait(uint64_t id){
  while (*(volatile char *)id == 0){};
  *(char *)id = 0;
  return Status{};
}

Status ReadConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = (void *)((size_t)region.addr-1);
  mr.length = region.length+1;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

StatusOr<ReceiveRegion> ReadConnection::RegisterReceiveRegion(void *addr, uint32_t length){
  struct ibv_mr * mr = ibv_reg_mr(this->ep_->GetPd(), addr, length, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  ReceiveRegion reg;
  reg.addr = addr;
  reg.length = length;
  reg.lkey = mr->lkey;
  reg.context = (uint64_t)mr;
  return reg;
}
Status ReadConnection::DeregisterReceiveRegion(ReceiveRegion reg){
  int ret = ibv_dereg_mr((struct ibv_mr *)reg.context);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "free: error deregistering mr");
  }
  return Status();
}


StatusOr<ReadRequest> ReadConnection::ReceiveRequest(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  struct ibv_wc wc = wcStatus.value();
  auto mr = this->rq_->GetMR(wc.wr_id);
  ReadRequest *req = (ReadRequest *)mr.addr;
  // We need to copy to stack o/w it might be overwritten
  ReadRequest res;
  res.length = req->length;
  res.addr = req->addr;
  res.key = req->key;
  auto stat = this->rq_->PostMR(wc.wr_id);
  if (!stat.ok()){
    return stat;
  }
  return res;
}

Status ReadConnection::WaitReceive(uint64_t id){
  while (this->ackd_rcv_id_ < id){
    auto wcStatus = this->ep_->PollSendCq();
    if (!wcStatus.ok()){
      return wcStatus.status();
    }
    ibv_wc wc = wcStatus.value();
    this->ackd_rcv_id_ = wc.wr_id;
  }
  if (!this->fence_) {
    struct ibv_send_wr  *bad;
    auto stat = this->ep_->PostSendRaw(&this->outstanding_read_wr_[id%inflight], &bad);
    if (!stat.ok()){
      std::cerr << "bad_id " << bad->wr_id << std::endl;
      return stat;
    }
  }
  return Status();
}

StatusOr<uint64_t> ReadConnection::ReadInto(void *addr, uint32_t key, ReadRequest req){
  if (this->next_rcv_id_ > this->ackd_rcv_id_ + inflight){
    return Status(StatusCode::RateLimit, "Too many outstanding requests");
  }

  struct ibv_sge sge_ack;
  struct ibv_send_wr wr_ack;
  uint64_t id = this->next_rcv_id_++;
  sge_ack.addr = (uintptr_t)&one;
  sge_ack.length = sizeof(char);

  wr_ack.wr_id = id;
  wr_ack.next = NULL;
  wr_ack.sg_list = &sge_ack;
  wr_ack.num_sge = 1;
  wr_ack.opcode = IBV_WR_RDMA_WRITE;
  wr_ack.wr.rdma.remote_addr = req.addr-1;
  wr_ack.wr.rdma.rkey = req.key;
  wr_ack.send_flags = this->fence_ ? IBV_SEND_SIGNALED | IBV_SEND_INLINE | IBV_SEND_FENCE : IBV_SEND_INLINE;  

  if (!this->fence_){
    this->outstanding_read_sge_[id%inflight] = sge_ack;
    wr_ack.sg_list = &this->outstanding_read_sge_[id%inflight];
    this->outstanding_read_wr_[id%inflight] = wr_ack;
  }
  
  struct ibv_sge sge_data;
  sge_data.addr = (size_t)addr;
  sge_data.length = req.length;
  sge_data.lkey = key;

  struct ibv_send_wr  *bad;
  struct ibv_send_wr wr_data;
  wr_data.wr_id = id;
  wr_data.next = this->fence_? &wr_ack : NULL;
  wr_data.sg_list = &sge_data;
  wr_data.num_sge = 1;
  wr_data.opcode = IBV_WR_RDMA_READ;
  wr_data.wr.rdma.remote_addr = req.addr;
  wr_data.wr.rdma.rkey = req.key;
  wr_data.send_flags = this->fence_ ? 0 : IBV_SEND_SIGNALED;  

  auto stat = this->ep_->PostSendRaw(&wr_data, &bad);
  if (!stat.ok()){
    std::cerr << "bad_id " << bad->wr_id << std::endl;
    return stat;
  }
  return id;
}


StatusOr<ReceiveRegion> ReadConnection::Receive(){
  auto req_s = this->ReceiveRequest();
  if (!req_s.ok()){
    return req_s.status();
  }
  ReadRequest req = req_s.value();
  
  auto regStat = this->allocator_->Alloc(req.length);
  if (!regStat.ok()){
    return regStat.status();
  }
  auto reg = regStat.value();

  ReceiveRegion rcvReg;
  rcvReg.addr = reg.addr;
  rcvReg.length = reg.length;
  rcvReg.context = reg.context;

  auto id_s = this->ReadInto(rcvReg.addr, reg.lkey, req);
  if (!id_s.ok()){
    return id_s.status();
  }
  auto stat = this->WaitReceive(id_s.value());
  if (!stat.ok()){
    return stat;
  }
  return rcvReg;
}
StatusOr<uint32_t> ReadConnection::Receive(ReceiveRegion reg){
  auto req_s = this->ReceiveRequest();
  if (!req_s.ok()){
    return req_s.status();
  }
  ReadRequest req = req_s.value();
  if (reg.length < req.length){
    return Status(StatusCode::InvalidArgument, "Recieve Region to small");
  }
  auto id_s = this->ReadInto(reg.addr, reg.lkey, req);
  if (!id_s.ok()){
    return id_s.status();
  }
  auto stat = this->WaitReceive(id_s.value());
  if (!stat.ok()){
    return stat;
  }
  return req.length;
}

StatusOr<uint64_t> ReadConnection::ReceiveAsync(ReceiveRegion reg){
  return this->ReceiveAsync(reg, nullptr);
}

StatusOr<uint64_t> ReadConnection::ReceiveAsync(ReceiveRegion reg, uint32_t *length){
  auto req_s = this->ReceiveRequest();
  if (!req_s.ok()){
    return req_s.status();
  }
  ReadRequest req = req_s.value();
  if (reg.length < req.length){
    return Status(StatusCode::InvalidArgument, "Recieve Region to small");
  }
  if (length){
    *length = req.length;
  }
  return this->ReadInto(reg.addr, reg.lkey, req);
}



Status ReadConnection::Free(ReceiveRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

}
}
