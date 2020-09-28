#include "ring_buffer.hpp"

#include "ring_buffer/magic_buffer.hpp"
#include "error.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <infiniband/verbs.h>
#include <string>

namespace kym {
namespace ringbuffer {

void *BasicRingBuffer::Read(uint32_t len) {
  // We either read at the read_ptr or a the beginning of the buffer if there is not enough space
  uint32_t read_offset = (this->read_ptr_ + len <= this->length_) ? this->read_ptr_ : 0;
  this->read_ptr_ = this->add(this->read_ptr_, len);
  this->outstanding_.push_back(read_offset);
  return (void *)((size_t)this->addr_ + read_offset);
}

void *BasicRingBuffer::GetReadPtr() {
  return (void *)((size_t)this->addr_ + this->read_ptr_);
}

uint32_t BasicRingBuffer::GetReadOff() {
  return this->read_ptr_;
}

uint32_t BasicRingBuffer::Free(void *addr) {
  uint32_t region_start = (size_t)addr - (size_t)this->addr_;
  
  auto front  = this->outstanding_.begin();
  if (region_start == *front){
    front++;
    this->head_ = front == this->outstanding_.end() ? this->read_ptr_ : *front;
  }
  this->outstanding_.remove(region_start);

  return this->head_;
}

BufferContext BasicRingBuffer::GetContext(){
  return BufferContext{0};
}

Status BasicRingBuffer::Close() {
  int ret = ibv_dereg_mr(this->mr_);
  if (ret) {
    Status(StatusCode::Internal, "Error " + std::to_string(ret) + " deregistering mr");
  }
  return Status();
}

uint32_t BasicRingBuffer::add(uint32_t offset, uint32_t len) {
  return (offset + len <= this->length_) ? offset + len : len;
}

void BasicRemoteBuffer::UpdateHead(uint32_t head){
  this->head_ = head;
}
uint32_t BasicRemoteBuffer::GetKey() {
  return this->mr_->lkey;
}
uint32_t BasicRemoteBuffer::GetTail(){
  return this->tail_;
}
StatusOr<uint64_t> BasicRemoteBuffer::GetWriteAddr(uint32_t len){
  // Check if there is space to send
  if (this->head_ == this->tail_ && this->full_){
    return Status(StatusCode::Unknown, "buffer is full");
  } else if (this->head_ == this->tail_ && len > this->length_){
    // We are empty, but the message is larger then our complete buffer.
    return Status(StatusCode::Unknown, "message larger then buffer");
  } else if (this->head_ < this->tail_ 
      && this->tail_ + len > this->length_
      && len > this->head_){
    // The free region does "wrap around" the end of the buffer. It either needs to fit in front of the used space or
    // behind it.
    return Status(StatusCode::Unknown, "not enough free space for message");
  } else if (this->head_ > this->tail_ 
      && this->head_ - this->tail_ < len){
    // The free region is continuous. The message needs to be at most as big. 
    return Status(StatusCode::Unknown, "not enough free space for message");
  }

  uint64_t read_offset = (this->tail_ + len  <= this->length_) ? this->tail_ : 0;
  return this->addr_ + read_offset;
}

StatusOr<uint32_t> BasicRemoteBuffer::GetNextTail(uint32_t len){
  return Status(StatusCode::NotImplemented);
}

StatusOr<uint64_t> BasicRemoteBuffer::Write(uint32_t len){
  auto tail_s = this->GetWriteAddr(len);
  if (!tail_s.ok()){
    return tail_s;
  }
  this->tail_ = tail_s.value() - this->addr_;
  std::cout << "new tail " << this->tail_;
  this->full_ = false;
  return tail_s;
}



StatusOr<MagicRingBuffer*> NewMagicRingBuffer(struct ibv_pd *pd, uint32_t size){
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
  return new MagicRingBuffer(mr);
}

MagicRingBuffer::MagicRingBuffer(struct ibv_mr *mr) : mr_(mr){
  this->addr_ = mr->addr;
  this->length_ = mr->length/2;

  this->head_ = 0;
  this->read_ptr_ = 0;
}

void *MagicRingBuffer::Read(uint32_t len) {
  uint32_t read_offset = this->read_ptr_;
  this->read_ptr_ = (this->read_ptr_ + len) % this->length_;
  this->outstanding_.push_back(read_offset);
  return (void *)((size_t)this->addr_ + read_offset);
}

void *MagicRingBuffer::GetReadPtr() {
  return (void *)((size_t)this->addr_ + this->read_ptr_);
}

uint32_t MagicRingBuffer::GetReadOff() {
  return this->read_ptr_;
}

uint32_t MagicRingBuffer::Free(void *addr) {
  uint32_t region_start = (size_t)addr - (size_t)this->addr_;
  
  auto front  = this->outstanding_.begin();
  if (region_start == *front){
    front++;
    this->head_ = front == this->outstanding_.end() ? this->read_ptr_ : *front;
  }
  this->outstanding_.remove(region_start);

  return this->head_;
}

BufferContext MagicRingBuffer::GetContext(){
  auto ctx = BufferContext{0};

  *(uint64_t *)ctx.data = (uint64_t)this->addr_;
  ctx.data[2] = this->mr_->lkey;
  ctx.data[3] = this->length_;
  return ctx;
}

Status MagicRingBuffer::Close() {
  int ret = ibv_dereg_mr(this->mr_);
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(ret) + " deregistering mr");
  }
  return FreeMagicBuffer(this->addr_, this->length_);
}

MagicRemoteBuffer::MagicRemoteBuffer(BufferContext ctx){
  this->addr_ = *(uint64_t *)ctx.data;
  this->key_ = ctx.data[2];
  this->length_ = ctx.data[3];

  this->head_ = 0;
  this->tail_ = 0;
  this->full_ = false;
}

void MagicRemoteBuffer::UpdateHead(uint32_t head){
  this->head_ = head;
  this->full_ = false;
}
uint32_t MagicRemoteBuffer::GetKey() {
  return this->key_;
}
uint32_t MagicRemoteBuffer::GetTail(){
  return this->tail_;
}
StatusOr<uint64_t> MagicRemoteBuffer::GetWriteAddr(uint32_t len){
  // Check if there is space to send
  if (this->head_ == this->tail_ && this->full_){
    return Status(StatusCode::Unknown, "buffer is full");
  } else if (this->head_ == this->tail_ && len > this->length_){
    // We are empty, but the message is larger then our complete buffer.
    return Status(StatusCode::Unknown, "message larger then buffer");
  } else if (this->head_ < this->tail_ 
      && (this->length_ - this->tail_) + this->head_ <= len){
    // The free region does "wrap around" the end of the buffer.
    return Status(StatusCode::Unknown, "not enough free space for message");
  } else if (this->head_ > this->tail_ 
      && this->head_ - this->tail_ <= len){
    // The free region is continuous. The message needs to be at most as big. 
    return Status(StatusCode::Unknown, "not enough free space for message");
  }

  return this->addr_ + this->tail_;
}
StatusOr<uint32_t> MagicRemoteBuffer::GetNextTail(uint32_t len){
  auto tail_s = this->GetWriteAddr(len);
  if (!tail_s.ok()){
    return tail_s.status();
  }
  return (this->tail_ + len)%this->length_;
}
StatusOr<uint64_t> MagicRemoteBuffer::Write(uint32_t len){
  auto tail_s = this->GetWriteAddr(len);
  if (!tail_s.ok()){
    return tail_s;
  }
  this->tail_ = (this->tail_ + len)%this->length_;
  this->full_ = (this->tail_ == this->head_);

  return tail_s;
}



}
}
