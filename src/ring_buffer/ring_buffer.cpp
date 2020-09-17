#include "ring_buffer.hpp"
#include "error.hpp"

#include <algorithm>
#include <bits/stdint-uintn.h>
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
StatusOr<uint32_t> BasicRemoteBuffer::GetWriteAddr(uint32_t len){
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

  return (this->tail_ + len  <= this->length_) ? this->tail_ : 0;
}
StatusOr<uint32_t> BasicRemoteBuffer::Write(uint32_t len){
  auto tail_s = this->GetWriteAddr(len);
  if (!tail_s.ok()){
    return tail_s;
  }
  this->tail_ = tail_s.value();
  return tail_s;
}


}
}
