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

uint32_t inflight = 100;
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


/*
 * Client Dial
 */

StatusOr<ReadConnection*> DialRead(std::string ip, int port){
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
  return new ReadConnection(ep, allocator, rq_status.value());
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
  return new ReadConnection(ep, allocator, rq_status.value());

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

  auto sendStatus = this->ep_->PostInline(0, &req, sizeof(req));
  if (!sendStatus.ok()){
    return sendStatus;
  }
  // ToDO(Fischi) We don't actually need to wait for this, but keeping track of these wr_ids is a little tedious so let's add it later
  auto wcStatus = this->ep_->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
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

// TODO(Fischi) We need some kind of async receive, o/w this will block a long time and we will not utilize nearly enough bw
StatusOr<ReceiveRegion> ReadConnection::Receive(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  struct ibv_wc wc = wcStatus.value();
  auto mr = this->rq_->GetMR(wc.wr_id);
  ReadRequest *req = (ReadRequest *)mr.addr;
  auto len = req->length;
  auto addr = req->addr;
  auto key = req->key;
  auto stat = this->rq_->PostMR(wc.wr_id);
  if (!stat.ok()){
    return stat;
  }

  auto regStat = this->allocator_->Alloc(len);
  if (!regStat.ok()){
    return regStat.status();
  }
  auto reg = regStat.value();

  ReceiveRegion rcvReg;
  rcvReg.addr = reg.addr;
  rcvReg.length = reg.length;
  rcvReg.context = reg.context;
  

  // TODO(Fischi) Join this in a single doorbell
  stat = this->ep_->PostRead(0, reg.lkey, reg.addr, reg.length, addr, key);
  if (!stat.ok()){
    return stat;
  }
  auto wc_stat = this->ep_->PollSendCq();
  if (!wc_stat.ok()){
    return wc_stat.status();
  }
  char one = 1;
  stat = this->ep_->PostWriteInline(0, &one, sizeof(one), addr-1, key);
  if (!stat.ok()){
    return stat;
  }
  auto rwcStatus = this->ep_->PollSendCq();
  if (!rwcStatus.ok()){
    return rwcStatus.status();
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
