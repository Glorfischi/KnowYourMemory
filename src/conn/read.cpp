#include "read.hpp"

#include <assert.h>

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdio>
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

#include "conn.hpp"
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
  .retry_count = 8,  
  .rnr_retry_count = 8, 
};


/*
 * Client Dial
 */

StatusOr<std::unique_ptr<ReadConnection>> DialRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto epStatus = kym::endpoint::Dial(ip, port, read_connection_default_opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::shared_ptr<endpoint::Endpoint> ep = epStatus.value();
  // TODO(fischi): Negotiate ack buffer
  auto conn = std::make_unique<ReadConnection>(ep);
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

  // TODO(Fischi) Negotiate ack buffer
  auto conn = std::make_unique<ReadConnection>(ep);
  return StatusOr<std::unique_ptr<ReadConnection>>(std::move(conn));
}

ReadListener::ReadListener(std::unique_ptr<endpoint::Listener> listener) : listener_(std::move(listener)) {}

Status ReadListener::Close() {
  return this->listener_->Close();
}



/*
 * ReadConnection
 */
ReadConnection::ReadConnection(std::shared_ptr<endpoint::Endpoint> ep) : ep_(ep) {
  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  this->sender_ = std::make_unique<ReadSender>(ep, allocator);
  this->receiver_ = std::make_unique<ReadReceiver>(ep, allocator);
}

Status ReadConnection::Close(){
  return this->ep_->Close();
}

StatusOr<SendRegion> ReadConnection::GetMemoryRegion(size_t size){
  return this->sender_->GetMemoryRegion(size);
}
Status ReadConnection::Send(SendRegion region){
  return this->sender_->Send(region);
}
Status ReadConnection::Free(SendRegion region){
  return this->sender_->Free(region);
}

StatusOr<ReceiveRegion> ReadConnection::Receive(){
  return this->receiver_->Receive();
}
Status ReadConnection::Free(ReceiveRegion region){
  return this->receiver_->Free(region);
}



/*
 * ReadSender
 */

ReadSender::ReadSender(std::shared_ptr<endpoint::Endpoint> ep, 
    std::shared_ptr<kym::memory::Allocator> allocator) : allocator_(allocator), ep_(ep) {
}

StatusOr<SendRegion> ReadSender::GetMemoryRegion(size_t size){
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
};

Status ReadSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}


Status ReadSender::Send(SendRegion region){
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
  // TODO(Fischi) Ack
  return Status();
}

/*
 * ReadReceiver
 */
ReadReceiver::ReadReceiver(std::shared_ptr<endpoint::Endpoint> ep, 
    std::shared_ptr<kym::memory::Allocator> allocator) : ep_(ep), allocator_(allocator) {
  // TODO(fischi) Parameterize
  size_t transfer_size = sizeof(struct ReadRequest);
  ibv_pd pd = this->ep_->GetPd();
  for (int i = 0; i < 10; i++){

    // TODO(Fischi) Maybe we should replace that was a call to an allocator?
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    // TODO(Fischi) Error handling. This should not be in the constructor
    auto regStatus = this->ep_->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    this->mrs_.push_back(mr);
  }
}

ReadReceiver::~ReadReceiver(){
  // TODO(Fischi) This might fail.. We cannot fail in destructors. Maybe we need some kind of close() method?
  for (auto mr : this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      std::cerr << "Error " << ret << std::endl;
    }
    free(mr->addr);
  }
}

StatusOr<ReceiveRegion> ReadReceiver::Receive(){
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
  return rcvReg;
}


Status ReadReceiver::Free(ReceiveRegion region) {
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

}
}
