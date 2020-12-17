#include "reverse_buffer.hpp"
#include "ring_buffer.hpp"

#include "ring_buffer/magic_buffer.hpp"
#include "error.hpp"

#include "debug.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <infiniband/verbs.h>
#include <string>

namespace kym {
namespace ringbuffer {

StatusOr<ReverseRingBuffer*> NewReverseRingBuffer(struct ibv_pd *pd, uint32_t size){
  auto buf_s = GetMagicBuffer(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating magic buffer");
  }
  void *buf = buf_s.value();
  memset(buf, 0, size);
  struct ibv_mr *mr = ibv_reg_mr(pd, buf, 2*size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ);
  if (mr == nullptr){
    perror("ERROR");
    FreeMagicBuffer(buf, size);
    return Status(StatusCode::Internal, "error registering mr");
  }
  debug(stderr, "Registered MR for reverse buffer\t[addr: %p, key: %d, length %ld, end: %p]\n", mr->addr, mr->rkey, mr->length, (char *)mr->addr + mr->length);  
  return new ReverseRingBuffer(mr);
}

ReverseRingBuffer::ReverseRingBuffer(struct ibv_mr *mr) : mr_(mr){
  this->addr_ = (char *)mr->addr;
  this->length_ = mr->length/2;

  this->head_ = 0;
  this->read_ptr_ = 0;
}

void *ReverseRingBuffer::Read(uint32_t len) {
  // Wrap arround front
  this->read_ptr_ = (this->read_ptr_ > len) ? this->read_ptr_ - len : this->length_ + this->read_ptr_ - len;
  uint32_t read_offset = this->read_ptr_;
  //debug(stderr, "Reverse buffer read\t[read_offset: %d]\n", read_offset);
  this->outstanding_.push_back(read_offset);
  return (void *)((size_t)this->addr_ + read_offset);
}

void *ReverseRingBuffer::GetReadPtr() {
  return (char *)this->addr_ + this->GetReadOff();
}

uint32_t ReverseRingBuffer::GetReadOff() {
  return (this->read_ptr_ > 0) ? this->read_ptr_ - 1 : this->length_ - 1;
}

uint32_t ReverseRingBuffer::Free(void *addr) {
  uint32_t region_start = (size_t)addr - (size_t)this->addr_;
  
  auto front  = this->outstanding_.begin();
  if (region_start == *front){
    front++;
    this->head_ = (front == this->outstanding_.end()) ? this->read_ptr_ : *front;
  }
  this->outstanding_.remove(region_start);

  return this->head_;
}

BufferContext ReverseRingBuffer::GetContext(){
  auto ctx = BufferContext{0};

  *(uint64_t *)ctx.data = (uint64_t)this->addr_;
  ctx.data[2] = this->mr_->lkey;
  ctx.data[3] = this->length_;
  return ctx;
}

Status ReverseRingBuffer::Close() {
  int ret = ibv_dereg_mr(this->mr_);
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(ret) + " deregistering mr");
  }
  return FreeMagicBuffer(this->addr_, this->length_);
}

ReverseRemoteBuffer::ReverseRemoteBuffer(BufferContext ctx){
  this->addr_ = *(uint64_t *)ctx.data;
  this->key_ = ctx.data[2];
  this->length_ = ctx.data[3];

  this->head_ = 0;
  this->tail_ = 0;
  this->full_ = false;
}

void ReverseRemoteBuffer::UpdateHead(uint32_t head){
  //if(this->head_ != head) info(stderr, "UpdateHead. Old %d, New %d, Diff %d\n", this->head_, head, head-this->head_);
  //info(stderr, "UpdateHead. Old %d, New %d, Diff %d\n", this->head_, head, head-this->head_);
  this->head_ = head;
  this->full_ = false;
}
uint32_t ReverseRemoteBuffer::GetKey() {
  return this->key_;
}
uint32_t ReverseRemoteBuffer::GetTail(){
  return this->tail_;
}

StatusOr<uint64_t> ReverseRemoteBuffer::GetWriteAddr(uint32_t len){
  auto tail_s = this->GetNextTail(len);
  if (!tail_s.ok()){
    return tail_s.status();
  }
  return this->addr_ + tail_s.value();
}
StatusOr<uint64_t> ReverseRemoteBuffer::GetWriteAddr(uint32_t len, bool *update){
  auto tail_s = this->GetNextTail(len);
  if (!tail_s.ok()){
    return tail_s.status();
  }
  // FIXME(Fischi) Just a quick hack
  int buf_size = 8*1024*1024;
  if (
      (this->head_ > this->tail_ && (this->length_ - this->head_) + this->tail_ <= buf_size/2 ) ||
      (this->head_ < this->tail_ && this->tail_ - this->head_ <= buf_size/2 )
    ){
    *update = true;
  }

  return this->addr_ + tail_s.value();
}
StatusOr<uint32_t> ReverseRemoteBuffer::GetNextTail(uint32_t len){
  // Check if there is space to send
  if (this->head_ == this->tail_ && this->full_){
    return Status(StatusCode::Unknown, "buffer is full");
  } else if (this->head_ == this->tail_ && len > this->length_){
    // We are empty, but the message is larger then our complete buffer.
    return Status(StatusCode::Unknown, "message larger then buffer");
  } else if (this->head_ > this->tail_ 
     && (this->length_ - this->head_) + this->tail_ <= len){
    // The free region does "wrap around" the end of the buffer.
    return Status(StatusCode::Unknown, "not enough free space for message");
  } else if (this->head_ < this->tail_ 
      && this->tail_ - this->head_ <= len){
    // The free region is continuous. The message needs to be at most as big. 
    return Status(StatusCode::Unknown, "not enough free space for message");
  }

  return (this->tail_ > len) ? this->tail_ - len : this->tail_ + this->length_ - len;
}
StatusOr<uint64_t> ReverseRemoteBuffer::Write(uint32_t len){
  auto tail_s = this->GetNextTail(len);
  if (!tail_s.ok()){
    return tail_s.status();
  }
  //debug(stderr, "Reverse buffer update tail\t[old_tail: %d, new_tail %d]\n", this->tail_, tail_s.value());
  this->tail_ = tail_s.value();
  this->full_ = (this->tail_ == this->head_);

  return this->addr_ + tail_s.value();
}



}
}
