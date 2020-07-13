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

  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  auto sender = std::make_unique<SendReceiveSender>(ep, allocator);
  auto receiver = std::make_unique<SendReceiveReceiver>(ep);
  auto conn = std::make_unique<SendReceiveConnection>(std::move(sender), std::move(receiver));
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

  std::shared_ptr<memory::DumbAllocator> allocator = std::make_shared<memory::DumbAllocator>(ep->GetPd());
  auto sender = std::make_unique<SendReceiveSender>(ep, allocator);
  auto receiver = std::make_unique<SendReceiveReceiver>(ep);
  auto conn = std::make_unique<SendReceiveConnection>(std::move(sender), std::move(receiver));
  return StatusOr<std::unique_ptr<SendReceiveConnection>>(std::move(conn));

}

SendReceiveListener::SendReceiveListener(std::unique_ptr<endpoint::Listener> listener) : listener_(std::move(listener)) {}

/*
 * SendReceiveConnection
 */
SendReceiveConnection::SendReceiveConnection(std::unique_ptr<SendReceiveSender> sender, 
    std::unique_ptr<SendReceiveReceiver> receiver) {
  this->sender_ = std::move(sender);
  this->receiver_ = std::move(receiver);
}

StatusOr<SendRegion> SendReceiveConnection::GetMemoryRegion(size_t size){
  return this->sender_->GetMemoryRegion(size);
}
Status SendReceiveConnection::Send(SendRegion region){
  return this->sender_->Send(region);
}
Status SendReceiveConnection::Free(SendRegion region){
  return this->sender_->Free(region);
}

StatusOr<ReceiveRegion> SendReceiveConnection::Receive(){
  return this->receiver_->Receive();
}
Status SendReceiveConnection::Free(ReceiveRegion region){
  return this->receiver_->Free(region);
}


/*
 * SendReceiveSender
 */

SendReceiveSender::SendReceiveSender(std::shared_ptr<endpoint::Endpoint> ep, 
    std::shared_ptr<kym::memory::Allocator> allocator) : allocator_(allocator), ep_(ep) {
}


StatusOr<SendRegion> SendReceiveSender::GetMemoryRegion(size_t size){
  assert(this->allocator_ != nullptr);
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

Status SendReceiveSender::Free(SendRegion region){
  assert(this->allocator_ != NULL);
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

Status SendReceiveSender::Send(SendRegion region){
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




/*
 * SendReceiveReceiver
 */
SendReceiveReceiver::SendReceiveReceiver(std::shared_ptr<endpoint::Endpoint> ep): ep_(ep) {
  // TODO(fischi) Parameterize
  size_t transfer_size = 1048576;
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


StatusOr<ReceiveRegion> SendReceiveReceiver::Receive() {
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

Status SendReceiveReceiver::Free(ReceiveRegion region) {
  assert((size_t)region.context < this->mrs_.size());
  struct ibv_mr *mr = this->mrs_[region.context];
  return this->ep_->PostRecv(region.context, mr->lkey, mr->addr, mr->length); 
}

}
}
