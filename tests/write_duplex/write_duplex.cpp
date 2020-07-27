#include "write_duplex.hpp"

#include <bits/stdint-uintn.h>
#include <iostream>
#include <ostream>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory> // For smart pointers


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"

#include "ring_buffer/magic_buffer.hpp"

#include "conn.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"
#include "receive_queue.hpp"

namespace kym {
namespace connection {

// Max number of outstanding sends
const int write_duplex_outstanding = 10;
const size_t write_duplex_buf_size = 10*1024*1024;

struct connectionInfo {
  uint64_t addr;
  uint32_t key;
};

endpoint::Options default_write_duplex_options = {
  .qp_attr = {
    .cap = {
      .max_send_wr = write_duplex_outstanding,
      .max_recv_wr = write_duplex_outstanding,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = sizeof(connectionInfo),
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 0,
  .initiator_depth =  write_duplex_outstanding,
  .retry_count = 8,  
  .rnr_retry_count = 7, 
};


Status sendCI(endpoint::Endpoint *ep, uint64_t addr, uint32_t key){
  // Sending info on buffer memory region
  connectionInfo ci;
  ci.key = key;
  ci.addr = addr;
  auto sendStatus = ep->PostInline(54, &ci, sizeof(ci)); 
  if (!sendStatus.ok()){
    return sendStatus;
  }
  auto wcStatus = ep->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  return Status();
}
StatusOr<connectionInfo> receiveCI(endpoint::Endpoint *ep){
  // Getting info on ring buffer
  auto pd = ep->GetPd();
  char* buf = (char*)malloc(sizeof(connectionInfo));
  struct ibv_mr * mr = ibv_reg_mr(&pd, buf, sizeof(connectionInfo), IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);  
  auto rcv_stat = ep->PostRecv(54, mr->lkey, mr->addr, mr->length);
  if (!rcv_stat.ok()){
    return rcv_stat;
  }
  auto rcv_wc_stat = ep->PollRecvCq();
  if (!rcv_wc_stat.ok()){
    return rcv_wc_stat.status();
  }
  struct ibv_wc rcv_wc = rcv_wc_stat.value();
  if (rcv_wc.wr_id != 54) {
    // Bad race
    return Status(StatusCode::Internal, "protocol missmatch");
  }
  connectionInfo ci = *(connectionInfo *)mr->addr;
  free(mr->addr);
  int ret = ibv_dereg_mr(mr);
  if (ret) {
    return Status(StatusCode::Unknown, "error dereg rcv mr");
  }
  return ci;
}


StatusOr<std::unique_ptr<WriteDuplexConnection>> DialWriteDuplex(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto ep_stat = kym::endpoint::Dial(ip, port, default_write_duplex_options);
  if (!ep_stat.ok()){
    return ep_stat.status();
  }

  std::shared_ptr<endpoint::Endpoint> ep = ep_stat.value();

  auto rq = endpoint::GetReceiveQueue(ep.get(), sizeof(uint32_t), write_duplex_outstanding);
  
  auto alloc = std::make_shared<memory::DumbAllocator>(ep->GetPd());

  auto magic_stat = ringbuffer::GetMagicBuffer(write_duplex_buf_size);
  if (!magic_stat.ok()){
    return magic_stat.status().Wrap("error initialzing magic buffer while dialing");
  }
  auto pd = ep->GetPd();
  struct ibv_mr *buf_mr = ibv_reg_mr(&pd, magic_stat.value(), 2 * write_duplex_buf_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

  auto stat = sendCI(ep.get(), write_duplex_buf_size, buf_mr->lkey);
  if (!stat.ok()){
    return stat.Wrap("error sending connection info while dialing"); 
  }
  auto ci_stat = receiveCI(ep.get());
  if (!ci_stat.ok()){
    return stat.Wrap("error receiving connection info while dialing"); 
  }
  connectionInfo ci = ci_stat.value();

  auto sndr = std::make_unique<WriteDuplexConnection>(ep, buf_mr, rq, alloc, ci.addr, ci.key);

  return std::move(sndr);
}

StatusOr<std::unique_ptr<WriteDuplexListener>> ListenWriteDuplex(std::string ip, int port){
  return Status(StatusCode::NotImplemented);
}


WriteDuplexListener::WriteDuplexListener(std::unique_ptr<endpoint::Listener> listener){
}
Status WriteDuplexListener::Close(){
  return Status(StatusCode::NotImplemented);
}
StatusOr<std::unique_ptr<WriteDuplexConnection>> WriteDuplexListener::Accept(){
  return Status(StatusCode::NotImplemented);
}


WriteDuplexConnection::WriteDuplexConnection(std::shared_ptr<endpoint::Endpoint> ep, struct ibv_mr *buf_mr, 
    std::shared_ptr<kym::endpoint::IReceiveQueue> rq, std::shared_ptr<memory::Allocator> allocator, 
    uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_){
  this->ep_ = ep;
  this->rq_ = rq;
  this->allocator_ = allocator;

  this->rbuf_vaddr_ = rbuf_vaddr_;
  this->rbuf_rkey_ = rbuf_rkey_;
  this->rbuf_full_ = false;
  this->rbuf_head_ = 0;
  this->rbuf_tail_ = 0;
  this->rbuf_size_ = write_duplex_buf_size;
}
Status WriteDuplexConnection::Close(){
  // TODO(Fischi) Free ring buffer
  auto stat = this->ep_->Close();
  if (!stat.ok()){
    return stat;
  }
  return this->ep_->Close();
}
StatusOr<ReceiveRegion> WriteDuplexConnection::Receive(){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Free(ReceiveRegion){
  return Status(StatusCode::NotImplemented);
}
StatusOr<SendRegion> WriteDuplexConnection::GetMemoryRegion(size_t size){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Send(SendRegion region){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Free(SendRegion region){
  return Status(StatusCode::NotImplemented);
}


}
}
