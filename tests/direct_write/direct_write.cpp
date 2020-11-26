#include "direct_write.hpp"


#include <cstdint>
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

const uint64_t inflight = 300;

endpoint::Options defaultOptions = {
  .pd = NULL,
  .qp_attr = {
    .qp_context = NULL,
    .send_cq = NULL,
    .recv_cq = NULL,
    .srq = NULL,
    .cap = {
      .max_send_wr = inflight,
      .max_recv_wr = inflight,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 0,
    },
    .qp_type = IBV_QPT_RC,
    .sq_sig_all = 0,
  },
  .use_srq = false,
  .private_data = NULL,
  .private_data_len = 0,
  .responder_resources = 0,
  .initiator_depth =  0,
  .flow_control = 0,
  .retry_count = 0,  
  .rnr_retry_count = 0, 
  .native_qp = false,
  .inline_recv = 0,
};

inline uint32_t *getLengthAddr(void *addr, uint32_t max_len){
  return nullptr;
}

}

StatusOr<SendRegion> DirectWriteConnection::GetMemoryRegion(size_t size){
  auto mrStatus =  this->allocator_->Alloc(this->buf_size_); 
  if (!mrStatus.ok()){
    return mrStatus.status();
  }
  auto mr = mrStatus.value();
  uint32_t *end = getLengthAddr(mr.addr, this->buf_size_);
  *end = size;
  struct SendRegion reg = {0};
  reg.context = mr.context;
  reg.addr = mr.addr;
  reg.length = mr.length;
  reg.lkey = mr.lkey;
  return reg;
}
Status DirectWriteConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = this->buf_size_;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}

Status DirectWriteConnection::Send(SendRegion region){
  auto id_stat = this->SendAsync(region);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value());
}
StatusOr<uint64_t> DirectWriteConnection::SendAsync(SendRegion region){
  uint64_t id = this->next_id_++;
  
  DirectWriteReceiveBuffer buf = this->target_buf_[id%this->nr_buffers_];
  if (!buf.valid){
    return Status(StatusCode::RateLimit, "No receive buffer ready");
  }
  uint64_t addr = buf.addr;
  uint32_t rkey = buf.rkey;
  // We will send the complete buffer everytime.. That might be far too slow
  auto sendStatus = this->ep_->PostWrite(id, region.lkey, region.addr, this->buf_size_, addr, rkey); 
  if (!sendStatus.ok()){
    return sendStatus;
  }

  this->target_buf_[id%this->nr_buffers_].valid = false; // invalidate buffer
  return id;
}
Status DirectWriteConnection::Wait(uint64_t id){
  while (this->ackd_id_ < id){
    auto wcStatus = this->ep_->PollSendCq();
    if (!wcStatus.ok()){
      return wcStatus.status();
    }
    ibv_wc wc = wcStatus.value();
    this->ackd_id_ = wc.wr_id;
  }
  return Status();
}


StatusOr<ReceiveRegion> DirectWriteConnection::Receive(){
  void *addr = (void *)this->rcv_buffers_[this->rcv_head_].addr;
  volatile uint32_t *msg_len = getLengthAddr(addr, this->buf_size_);
  while(*msg_len == 0){
  }
  ReceiveRegion reg = {0};
  reg.addr = addr;
  reg.length = *msg_len;

  this->rcv_buffers_[this->rcv_head_].valid = false; // invalidate and advance
  this->rcv_head_ = (this->rcv_head_ + 1) % this->nr_buffers_;

  return reg;
}
Status DirectWriteConnection::Free(ReceiveRegion region){
  // Write to tail of remote rcv buf
  DirectWriteReceiveBuffer buf = { (uint64_t)region.addr, region.lkey, true };

  volatile uint32_t *msg_len = getLengthAddr(region.addr, this->buf_size_);
  *msg_len = 0;

  auto sendStatus = this->ep_->PostWriteInline(region.context, &buf, sizeof(buf), this->r_rcv_buf_addr_+this->r_rcv_tail_, this->r_rcv_buf_key_);
  if (!sendStatus.ok()){
    return sendStatus;
  }
  // TODO(fischi) look at send cq
  
  this->r_rcv_tail_++;
  this->rcv_buffers_[this->rcv_tail_++] = buf;

  return Status();
}

Status DirectWriteConnection::Close(){

  int ret = ibv_dereg_mr(this->target_buf_mr_);
  if (ret){
    return Status(StatusCode::Internal, "error deregistering target mr");
  }
  free(this->target_buf_);

  for (auto mr : this->rcv_buffer_mrs_){
    void *addr = mr->addr;
    int ret = ibv_dereg_mr(this->target_buf_mr_);
    if (ret){
      return Status(StatusCode::Internal, "error deregistering rcv mr");
    }
    free(addr);
  }
  
  return this->ep_->Close();
}
DirectWriteConnection::~DirectWriteConnection(){
  delete this->ep_;
  delete this->allocator_;
}

StatusOr<DirectWriteConnection *> DialDirectWrite(std::string ip, int port, int32_t buf_size){
  return Status(StatusCode::NotImplemented);
}
StatusOr<DirectWriteConnection *> DirectWriteListener::Accept(int32_t buf_size){
  return Status(StatusCode::NotImplemented);
}

DirectWriteListener::~DirectWriteListener(){
  delete this->listener_;
}
Status DirectWriteListener::Close(){
  return this->listener_->Close();
}

StatusOr<DirectWriteListener *> ListenDirectWrite(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  endpoint::Listener *ln = lnStatus.value();
  return new DirectWriteListener(ln);
}


}
}


