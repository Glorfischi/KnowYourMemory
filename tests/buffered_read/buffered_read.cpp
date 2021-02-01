#include "buffered_read.hpp"

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

#include "debug.h"

#include "mm/dumb_allocator.hpp"
#include "receive_queue.hpp"

#include "ring_buffer/magic_buffer.hpp"


namespace kym {
namespace connection {

namespace {
  const uint32_t inflight = 200;
  const uint32_t default_buffer_size = 8*1024*1024;
  endpoint::Options default_opts = {
    .qp_attr = {
      .cap = {
        .max_send_wr = inflight,
        .max_recv_wr = inflight,
        .max_send_sge = 1,
        .max_recv_sge = 1,
        .max_inline_data = 0,
      },
      .qp_type = IBV_QPT_RC,
    },
    .responder_resources = 16,
    .initiator_depth =  16,
    .retry_count = 1,  
    .rnr_retry_count = 0, 
  };

  struct conn_details { // 36 bytes
    uint32_t meta_rkey; // 4 bytes
    uint64_t head_addr; // 8 bytes
    uint64_t tail_addr; // 8 bytes
    uint32_t buffer_rkey; // 4 bytes
    uint64_t buffer_addr; // 8 bytes
    uint32_t buffer_len; // 4 bytes
  };


}

uint64_t BufferedReadConnection::GetMeanMsgSize() {
  if (this->d_sent_msgs_ == 0) {
    return 0;
  }
  return this->d_sent_bytes_/this->d_sent_msgs_;
}

  
StatusOr<ReceiveRegion> BufferedReadConnection::Receive(){
  if (*this->remote_tail_ == this->read_ptr_){
    // We don't have messages buffered
    while (*this->remote_tail_ == this->read_ptr_) {
      auto stat = this->ep_->PostRead(8, this->remote_meta_data_->lkey, (void *)this->remote_tail_, 
          sizeof(uint64_t), this->remote_tail_addr_, this->remote_meta_rkey_);
      if (!stat.ok()){
        return stat.Wrap("error posting read to get tail");
      }
      auto wc_s = this->ep_->PollSendCq();
      if (!wc_s.ok()){
        return wc_s.status().Wrap("error polling send cq to get tail");
      }
    }
    uint64_t len = *this->remote_tail_ - this->read_ptr_;
    uint32_t offset = this->read_ptr_ % this->remote_buf_len_;
    debug(stderr, "Reading from addr %p to %p, offset %d\n", (void *)(this->remote_buf_addr_ + offset), (void *)((uint64_t)this->remote_buffer_->addr + offset), offset);
    auto stat = this->ep_->PostRead(8, this->remote_buffer_->lkey, (void *)((uint64_t)this->remote_buffer_->addr + offset), 
        len,  this->remote_buf_addr_ + offset, this->remote_buf_rkey_);
    auto wc_s = this->ep_->PollSendCq();
    if (!wc_s.ok()){
      return wc_s.status().Wrap("error polling send cq to move data");
    }
    this->d_sent_msgs_++;
    this->d_sent_bytes_ += wc_s.value().byte_len;
  }

  debug(stderr, "Got new data [tail: %d, read_ptr %d]\n", *this->remote_tail_, this->read_ptr_);

  uint32_t offset = this->read_ptr_ % this->remote_buf_len_;
  uint64_t addr = (uint64_t)this->remote_buffer_->addr + offset;
  uint32_t len = *(uint32_t *)addr; // segfault after free?
  debug(stderr, "New buffer [addr %p, offset %d, len: %d]\n", (void *)addr, offset, len);
  this->read_ptr_ += len + sizeof(uint32_t);
  
  this->outstanding_.push_back(addr);
  return ReceiveRegion{addr, (void *)(addr + sizeof(uint32_t)), len, 0};
}

Status BufferedReadConnection::Free(ReceiveRegion reg){
  uint64_t reg_start = reg.context;
  auto front  = this->outstanding_.begin();
  if (reg_start == *front){ // this next in line to be freed
    front++; // Go to next unfreed
    *this->remote_head_ = (front == this->outstanding_.end()) ? this->read_ptr_ : *front; // if the list now is not empty 
    // Set head to next item o/w to read ptr
  }
  this->outstanding_.remove(reg_start); 

  // TODO(Fischi) Only update occasionally - Maybe we could actually add this to receive?
  /*debug(stderr, "writing to remote head [head: %p, remote head addr %p]\n", 
      (void *)this->remote_head_, (void *)this->remote_head_addr_);*/
  if (*this->remote_head_ - this->acked_head_ > this->remote_buf_len_/3) {
    auto stat = this->ep_->PostWrite(16, this->remote_meta_data_->lkey, (void *)this->remote_head_, 
            sizeof(uint64_t), this->remote_head_addr_, this->remote_meta_rkey_);
    if (!stat.ok()){
      return stat.Wrap("error updating head at remote");
    }
    auto wc_s = this->ep_->PollSendCq();
    if (!wc_s.ok()){
      return wc_s.status().Wrap("error polling send cq to update heaupdate headd");
    }
    this->acked_head_ = *this->remote_head_;
  }
  return Status();
}


Status BufferedReadConnection::Send(void *buf, uint32_t len){
  uint32_t tail = *this->tail_ % this->length_;
  bool free_space = false;

  int rnr_i = 0;
  while (!free_space) {
    uint32_t head = *this->head_ % this->length_;
    /*rnr_i++;
    if (rnr_i > 1) info(stderr, "Sender cannot write %d\n", rnr_i);*/
    if ( head == tail && len > this->length_){
      // We are empty, but the message is larger then our complete buffer.
      return Status(StatusCode::Unknown, "message larger then buffer");
    } else if (head < tail 
        && (this->length_ - tail) + head <= len){
      // The free region does "wrap around" the end of the buffer.
      continue;
    } else if (head > tail && head - tail <= len){
      // The free region is continuous. The message needs to be at most as big. 
      continue;
    }
    free_space = true;
  }
  
  size_t addr = (size_t)this->buffer_->addr + tail;
  debug(stderr, "Sending [addr %p, len %d]\n", (void *)addr, len);
  *(uint32_t *)addr = len;
  memcpy((void *)(addr + sizeof(uint32_t)), buf, len);
  // TODO(fischi) Memory barrier?
  *this->tail_ += len + sizeof(uint32_t);
  return Status();
}

Status BufferedReadConnection::Close(){
  auto stat = this->ep_->Close();
  if (!stat.ok()){
    return stat.Wrap("error closing endpoint");
  }
  void *addr = this->buffer_->addr;
  ibv_dereg_mr(this->buffer_);
  stat = ringbuffer::FreeMagicBuffer(addr, this->length_);
  if (!stat.ok()){
    return stat.Wrap("error freeing send buffer");
  }
  addr = this->meta_data_->addr;
  ibv_dereg_mr(this->meta_data_);
  free(addr);
  addr = this->remote_buffer_->addr;
  ibv_dereg_mr(this->remote_buffer_);
  stat = ringbuffer::FreeMagicBuffer(this->remote_buffer_->addr, this->remote_buf_len_);
  if (!stat.ok()){
    return stat.Wrap("error freeing receive buffer");
  }
  addr = this->remote_meta_data_->addr;
  ibv_dereg_mr(this->remote_meta_data_);
  free(addr);
  return Status();
}
BufferedReadConnection::~BufferedReadConnection(){
  delete this->ep_;
}


StatusOr<SendRegion> BufferedReadConnection::GetMemoryRegion(size_t size) {
  struct SendRegion reg = {0};
  reg.addr = calloc(1, size);
  reg.length = size;
  return reg;

}
Status BufferedReadConnection::Free(SendRegion region) {
  free(region.addr);
  return Status();
}
Status BufferedReadConnection::Send(SendRegion region) {
  return this->Send(region.addr, region.length);
}
StatusOr<uint64_t> BufferedReadConnection::SendAsync(SendRegion region) {
  return this->Send(region.addr, region.length);
}
Status BufferedReadConnection::Wait(uint64_t id){
  return Status();
}


StatusOr<BufferedReadConnection *> DialBufferedRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto opts = default_opts;
  auto ep_s = endpoint::Create(ip, port, opts);
  if (!ep_s.ok()){
    return ep_s.status().Wrap("error setting up endpoint");
  }
  endpoint::Endpoint *ep = ep_s.value();
  struct ibv_pd *pd = ep->GetPd();

  // Allocate local metadata
  void *meta_buf = calloc(2, sizeof(uint64_t));
  struct ibv_mr *metadata = ibv_reg_mr(pd, meta_buf, 2*sizeof(uint64_t), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
  if (metadata == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering metadata MR");
  }
  // Allocate local buffer
  auto mbuf_s = ringbuffer::GetMagicBuffer(default_buffer_size);
  if (!mbuf_s.ok()){
    free(meta_buf);
    ibv_dereg_mr(metadata);
    return mbuf_s.status().Wrap("error allocating local send buffer");
  }
  struct ibv_mr *send_buffer = ibv_reg_mr(pd, mbuf_s.value(), 2*default_buffer_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
  if (send_buffer == nullptr){
    free(meta_buf);
    ibv_dereg_mr(metadata);
    ringbuffer::FreeMagicBuffer(mbuf_s.value(), default_buffer_size);
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering send buffer MR");
  }

  // Connect to remote
  conn_details local_conn_details;
  local_conn_details.buffer_addr = (uint64_t)send_buffer->addr;
  local_conn_details.buffer_len = default_buffer_size;
  local_conn_details.buffer_rkey = send_buffer->lkey;
  local_conn_details.meta_rkey = metadata->lkey;
  local_conn_details.head_addr = (uint64_t)metadata->addr;
  local_conn_details.tail_addr = (uint64_t)metadata->addr + sizeof(uint64_t);
  opts.private_data = &local_conn_details;
  opts.private_data_len = sizeof(local_conn_details);
  auto stat = ep->Connect(opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  // Allocate remote metadata
  void *rmeta_buf = calloc(2, sizeof(uint64_t));
  struct ibv_mr *remote_metadata = ibv_reg_mr(pd, rmeta_buf, 2*sizeof(uint64_t), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
  if (metadata == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering remote metadata MR");
  }
  debug(stderr, "metadata cache [addr %p, key %d, len %d]\n", remote_metadata->addr, remote_metadata->lkey, remote_metadata->length);

  // Allocate receive buffer
  auto mrcvbuf_s = ringbuffer::GetMagicBuffer(remote_conn_details->buffer_len);
  if (!mrcvbuf_s.ok()){
    return mrcvbuf_s.status().Wrap("error allocating local send buffer");
  }
  struct ibv_mr *receive_buffer = ibv_reg_mr(pd, mbuf_s.value(), 2*remote_conn_details->buffer_len, IBV_ACCESS_LOCAL_WRITE);
  if (send_buffer == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering receive buffer MR");
  }

  return new BufferedReadConnection(ep, metadata, send_buffer, default_buffer_size,
      remote_metadata, remote_conn_details->meta_rkey, remote_conn_details->head_addr, remote_conn_details->tail_addr,
      receive_buffer, remote_conn_details->buffer_rkey, remote_conn_details->buffer_addr, remote_conn_details->buffer_len);
}


BufferedReadListener::~BufferedReadListener(){
  delete this->listener_;
}

Status BufferedReadListener::Close(){
  return this->listener_->Close();
}

StatusOr<BufferedReadConnection*> BufferedReadListener::Accept(){
  struct ibv_pd *pd = this->listener_->GetPd();
  auto opts = default_opts;

  // Allocate local metadata
  void *meta_buf = calloc(2, sizeof(uint64_t));
  struct ibv_mr *metadata = ibv_reg_mr(pd, meta_buf, 2*sizeof(uint64_t), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);
  if (metadata == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering metadata MR");
  }
  
  debug(stderr, "metadata [addr %p, key %d, buf_addr %p, len %d]\n", metadata->addr, metadata->lkey, meta_buf, metadata->length);
  // Allocate local buffer
  auto mbuf_s = ringbuffer::GetMagicBuffer(default_buffer_size);
  if (!mbuf_s.ok()){
    free(meta_buf);
    ibv_dereg_mr(metadata);
    return mbuf_s.status().Wrap("error allocating local send buffer");
  }
  struct ibv_mr *send_buffer = ibv_reg_mr(pd, mbuf_s.value(), 2*default_buffer_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
  if (send_buffer == nullptr){
    free(meta_buf);
    ibv_dereg_mr(metadata);
    ringbuffer::FreeMagicBuffer(mbuf_s.value(), default_buffer_size);
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering send buffer MR");
  }

  // Connect to remote
  conn_details local_conn_details;
  local_conn_details.buffer_addr = (uint64_t)send_buffer->addr;
  local_conn_details.buffer_len = default_buffer_size;
  local_conn_details.buffer_rkey = send_buffer->lkey;
  local_conn_details.meta_rkey = metadata->lkey;
  local_conn_details.head_addr = (uint64_t)metadata->addr;
  debug(stderr, "local tail addr %p\n", (void *)((uint64_t)metadata->addr + sizeof(uint64_t)));
  local_conn_details.tail_addr = (uint64_t)metadata->addr + sizeof(uint64_t);
  opts.private_data = &local_conn_details;
  opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(opts);
  if (!ep_s.ok()){
    return ep_s.status().Wrap("error accepting");
  }
  endpoint::Endpoint *ep = ep_s.value();

  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  // Allocate remote metadata
  void *rmeta_buf = calloc(2, sizeof(uint64_t));
  struct ibv_mr *remote_metadata = ibv_reg_mr(pd, rmeta_buf, 2*sizeof(uint64_t), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
  if (metadata == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering remote metadata MR");
  }

  // Allocate receive buffer
  auto mrcvbuf_s = ringbuffer::GetMagicBuffer(remote_conn_details->buffer_len);
  if (!mrcvbuf_s.ok()){
    return mrcvbuf_s.status().Wrap("error allocating local send buffer");
  }
  struct ibv_mr *receive_buffer = ibv_reg_mr(pd, mbuf_s.value(), 2*remote_conn_details->buffer_len, IBV_ACCESS_LOCAL_WRITE);
  if (send_buffer == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering receive buffer MR");
  }

  return new BufferedReadConnection(ep, metadata, send_buffer, default_buffer_size,
      remote_metadata, remote_conn_details->meta_rkey, remote_conn_details->head_addr, remote_conn_details->tail_addr,
      receive_buffer, remote_conn_details->buffer_rkey, remote_conn_details->buffer_addr, remote_conn_details->buffer_len);
}


StatusOr<BufferedReadListener *> ListenBufferedRead(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto lnStatus = endpoint::Listen(ip, port);
  if (!lnStatus.ok()){
    return lnStatus.status();
  }
  endpoint::Listener *ln = lnStatus.value();
  auto rcvLn = new BufferedReadListener(ln);
  return rcvLn;

}

}
}
