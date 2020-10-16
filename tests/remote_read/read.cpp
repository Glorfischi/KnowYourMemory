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


namespace kym {
namespace connection {

endpoint::Options read_connection_default_opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 1,
      .max_recv_wr = 10,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = sizeof(ReadRequest),
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 5,
  .initiator_depth =  5,
  .retry_count = 5,  
  .rnr_retry_count = 8, 
  .native_qp = false,
  .inline_recv = 0,
};


/*
 * Client Dial
 */

StatusOr<std::unique_ptr<ReadConnection>> DialRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto ep_status = kym::endpoint::Dial(ip, port, read_connection_default_opts);
  if (!ep_status.ok()){
    return ep_status.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = ep_status.value();


  // Negotiate ack buffer
  // FIXME(Fischi) We seem to have some kind of race. I don't know how.
  std::chrono::milliseconds timespan(100); // This is because of a race condition...
  std::this_thread::sleep_for(timespan);

  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  auto rcv_buf_stat = allocator->Alloc(sizeof(ReadRequest));
  if (!rcv_buf_stat.ok()){
    return rcv_buf_stat.status();
  }
  memory::Region rcv_buf = rcv_buf_stat.value();
  auto rcv_stat = ep->PostRecv(42, rcv_buf.lkey, rcv_buf.addr, rcv_buf.length);
  if (!rcv_stat.ok()){
    return rcv_stat;
  }

  auto ack_buf_stat = allocator->Alloc(sizeof(uint64_t));
  if (!ack_buf_stat.ok()){
    return ack_buf_stat.status();
  }
  memory::Region ack_buf = ack_buf_stat.value();
  *(uint64_t *)ack_buf.addr = 0;

  ReadRequest req;
  req.addr = (uint64_t)ack_buf.addr;
  req.key = ack_buf.lkey;
  ep->PostInline(0, &req, sizeof(req));
  auto send_wc_stat = ep->PollSendCq();
  if (!send_wc_stat.ok()){
    return send_wc_stat.status();
  }

  auto rcv_wc_stat = ep->PollRecvCq();
  if (!rcv_wc_stat.ok()){
    return rcv_wc_stat.status();
  }

  struct ibv_wc rcv_wc = rcv_wc_stat.value();
  if (rcv_wc.wr_id != 42) {
    // Bad race
    return Status(StatusCode::Unknown, "protocol missmatch");
  }
  ReadRequest *remote_buf = (ReadRequest *)rcv_buf.addr;

  auto conn = std::make_unique<ReadConnection>(ep, allocator, ack_buf, rcv_buf, remote_buf->addr, remote_buf->key);
  return StatusOr<std::unique_ptr<ReadConnection>>(std::move(conn));
}

/* 
 * Server listener
 */
StatusOr<std::unique_ptr<ReadListener>> ListenRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  std::unique_ptr<endpoint::Listener> ln = lnStatus.value();
  auto rcvLn = std::make_unique<ReadListener>(std::move(ln));
  return StatusOr<std::unique_ptr<ReadListener>>(std::move(rcvLn));
}

StatusOr<std::unique_ptr<ReadConnection>> ReadListener::Accept(){
  auto epStatus = this->listener_->Accept(read_connection_default_opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();

  // Negotiate ack buffer
  // FIXME(Fischi) We seem to have some kind of race. I don't know how.
  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  auto rcv_buf_stat = allocator->Alloc(sizeof(ReadRequest));
  if (!rcv_buf_stat.ok()){
    return rcv_buf_stat.status();
  }
  memory::Region rcv_buf = rcv_buf_stat.value();
  auto rcv_stat = ep->PostRecv(42, rcv_buf.lkey, rcv_buf.addr, rcv_buf.length);
  if (!rcv_stat.ok()){
    return rcv_stat;
  }

  auto ack_buf_stat = allocator->Alloc(sizeof(uint64_t));
  if (!ack_buf_stat.ok()){
    return ack_buf_stat.status();
  }
  memory::Region ack_buf = ack_buf_stat.value();
  *(uint64_t *)ack_buf.addr = 0;

  auto rcv_wc_stat = ep->PollRecvCq();
  if (!rcv_wc_stat.ok()){
    return rcv_wc_stat.status();
  }

  struct ibv_wc rcv_wc = rcv_wc_stat.value();
  if (rcv_wc.wr_id != 42) {
    // Bad race
    return Status(StatusCode::Unknown, "protocol missmatch");
  }
  ReadRequest *remote_buf = (ReadRequest *)rcv_buf.addr;


  ReadRequest req;
  req.addr = (uint64_t)ack_buf.addr;
  req.key = ack_buf.lkey;
  ep->PostInline(0, &req, sizeof(req));
  auto send_wc_stat = ep->PollSendCq();
  if (!send_wc_stat.ok()){
    return send_wc_stat.status();
  }
 
  auto conn = std::make_unique<ReadConnection>(ep, allocator, ack_buf, rcv_buf, remote_buf->addr, remote_buf->key);
  return StatusOr<std::unique_ptr<ReadConnection>>(std::move(conn));
}

ReadListener::ReadListener(std::unique_ptr<endpoint::Listener> listener) : listener_(std::move(listener)) {}

Status ReadListener::Close() {
  return this->listener_->Close();
}



/*
 * ReadConnection
 */
ReadConnection::ReadConnection(std::shared_ptr<endpoint::Endpoint> ep, std::shared_ptr<memory::Allocator> allocator,
    memory::Region ack, memory::Region ack_source, uint64_t ack_remote_addr, uint32_t ack_remote_key) 
  : ep_(ep), allocator_(allocator), ack_(ack), ack_send_buf_(ack_source), ack_remote_addr_(ack_remote_addr), ack_remote_key_(ack_remote_key) {
  // TODO(fischi) Parameterize
  size_t transfer_size = sizeof(struct ReadRequest);
  ibv_pd *pd = this->ep_->GetPd();
  for (int i = 0; i < 10; i++){
    // TODO(Fischi) Maybe we should replace that was a call to an allocator?
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    // TODO(Fischi) Error handling. This should not be in the constructor
    auto regStatus = this->ep_->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    this->mrs_.push_back(mr);
  }
}

Status ReadConnection::Close(){
  auto free_s = this->allocator_->Free(this->ack_);
  if (!free_s.ok()){
    return free_s;
  }
  for (auto mr : this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      return Status(StatusCode::Unknown, "Error deregistering receive region");
    }
    free(mr->addr);
  }
  return this->ep_->Close();
}

StatusOr<SendRegion> ReadConnection::GetMemoryRegion(size_t size){
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

Status ReadConnection::Send(SendRegion region){
  struct ReadRequest req;
  req.addr = (uint64_t)region.addr;
  req.key = region.lkey;
  req.length = region.length;

  auto sendStatus = this->ep_->PostInline(0, &req, sizeof(req));
  if (!sendStatus.ok()){
    return sendStatus;
  }
  auto wcStatus = this->ep_->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  uint64_t acked = 0;
  while(acked == 0){
    // TODO(fischi) Do we need volatile?
    acked = *(uint64_t *)this->ack_.addr;
  }
  *(uint64_t *)this->ack_.addr = 0;
  return Status();
}

Status ReadConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

StatusOr<ReceiveRegion> ReadConnection::Receive(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }

  struct ibv_wc wc = wcStatus.value();
  auto mr = this->mrs_[wc.wr_id];

  ReadRequest *req = (ReadRequest *)mr->addr;

  // Directly repost mr
  auto rcvStatus = this->ep_->PostRecv(wc.wr_id, mr->lkey, mr->addr, mr->length);
  if (!rcvStatus) {
    return rcvStatus;
  }


  auto regStat = this->allocator_->Alloc(req->length);
  if (!regStat.ok()){
    return regStat.status();
  }
  auto reg = regStat.value();

  ReceiveRegion rcvReg;
  rcvReg.addr = reg.addr;
  rcvReg.length = reg.length;
  rcvReg.context = reg.context;

  auto rStat = this->ep_->PostRead(0, reg.lkey, reg.addr, reg.length, req->addr, req->key);
  if (!rStat.ok()){
    return rStat;
  }
  auto rwcStatus = this->ep_->PollSendCq();
  if (!rwcStatus.ok()){
    return rwcStatus.status();
  }

  *(uint64_t *)this->ack_send_buf_.addr = 1;
  auto write_stat = this->ep_->PostWrite(0, this->ack_send_buf_.lkey, this->ack_send_buf_.addr, sizeof(uint64_t),
      this->ack_remote_addr_, this->ack_remote_key_);
  if (!write_stat.ok()){
    return write_stat;
  }

  return rcvReg;
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
