#include "reverse.hpp"

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>
#include <iostream>

#include <boost/crc.hpp>  
#include <string>

#include "conn.hpp"
#include "endpoint.hpp"
#include "error.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"
#include "receive_queue.hpp"
#include "ring_buffer/ring_buffer.hpp"
#include "ring_buffer/reverse_buffer.hpp"

#include "debug.h"

#include "acknowledge.hpp"



namespace kym {
namespace connection {

Status WriteReverseReceiver::Close() {
  return WriteReceiver::Close();
};

StatusOr<ReceiveRegion> WriteReverseReceiver::Receive(){
  ReceiveRegion reg;

  // We check the last byte. If this byte was written in all x86 systems we know the write is completed
  volatile char *next = (volatile char *)this->rbuf_->GetReadPtr();
  debug(stderr, "Spinning for next message\t[next: %p, offset: %d]\n", next, this->rbuf_->GetReadOff());  
  while (*next == 0){}
  // We cannot spin on the length, as we cannot be certain that the complete 4 bytes have been written when we
  // read it. Giving us some potentially interesting message lengths
  // Also we might acutally roll over the beginning of the buffer. To prevent that let's skip to the mirrored buffer..
  uint32_t msg_len = *(uint32_t *)(next + this->length_ - sizeof(uint32_t));

  
  void *addr = this->rbuf_->Read(msg_len);
  reg.addr = addr;
  reg.length = msg_len-sizeof(char)-sizeof(uint32_t); // Last 5 bytes are not interesting
  reg.lkey = 0;
  reg.context = 0;
  debug(stderr, "Receiving message\t[addr: %p, msg_length: %d, reg_length %d]\n", addr, msg_len, reg.length);  
  return reg;
}

Status WriteReverseReceiver::Free(ReceiveRegion reg){
  uint32_t head = this->rbuf_->Free(reg.addr);
  this->ack_->Ack(head);
  if ((head < this->acked_ && this->acked_ - head > this->max_unacked_)
      || (head > this->acked_ && this->acked_ + (this->length_ - head) > this->max_unacked_)){
    auto stat = this->ack_->Flush();
    if (!stat.ok()){
      return stat;
    }
    this->acked_ = head;
  }

  return Status();
}

StatusOr<SendRegion> WriteReverseSender::GetMemoryRegion(size_t size){
  SendRegion reg;
  // Add 6 bytes: 2 two indicator bytes (for us and for zeroing the next) and msg_len
  auto buf_s = this->alloc_->Alloc(size+2*sizeof(char)+sizeof(uint32_t)); 
  if (!buf_s.ok()){
    return buf_s.status();
  }
  auto buf = buf_s.value();
  reg.addr = (char *)buf.addr + sizeof(char); // First bit is not part of our buffer but indicator for the next
  reg.length = size;  
  reg.lkey = buf.lkey;
  reg.context = buf.context;

  // Set indicator byte
  *((char *)buf.addr + buf.length - sizeof(char)) = 1;
  *(uint32_t *)((char *)buf.addr + buf.length - sizeof(char) - sizeof(uint32_t)) = size + sizeof(uint32_t) + sizeof(char);

  return reg;
}

Status WriteReverseSender::Send(SendRegion reg){
  auto id_stat = this->SendAsync(reg);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}


StatusOr<uint64_t> WriteReverseSender::SendAsync(SendRegion reg){
  auto id = this->next_id_++;
  uint32_t len = reg.length+2*sizeof(char)+sizeof(uint32_t);
  auto addr_s = this->rbuf_->GetWriteAddr(len);
  if (!addr_s.ok()){
    auto head_s = this->ack_->Get();
    if (!head_s.ok()){
      return head_s.status().Wrap("error getting new head");
    }
    this->rbuf_->UpdateHead(head_s.value());
    addr_s = this->rbuf_->GetWriteAddr(len);
    while (!addr_s.ok()){
      head_s = this->ack_->Get();
      if (!head_s.ok()){
        return addr_s.status().Wrap(head_s.status().message());
      }
      this->rbuf_->UpdateHead(head_s.value());
      addr_s = this->rbuf_->GetWriteAddr(len);
    }
  }
  uint64_t addr = addr_s.value();

  auto stat = this->ep_->PostWrite(id, reg.lkey, (char *)reg.addr-sizeof(char), len, addr, this->rbuf_->GetKey());
  if (!stat.ok()){
    return stat;
  }
  // One byte does not belong to us.
  this->rbuf_->Write(len-sizeof(char));
  return id;
}

Status WriteReverseSender::Send(std::vector<SendRegion> regions){
  auto id_stat = this->SendAsync(regions);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}

StatusOr<uint64_t> WriteReverseSender::SendAsync(std::vector<SendRegion> regions){
  return Status(StatusCode::NotImplemented);
}

Status WriteReverseSender::Wait(uint64_t id){
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

Status WriteReverseSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = (char *)region.addr-sizeof(char);
  mr.length = region.length+2*sizeof(char)+sizeof(uint32_t);
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->alloc_->Free(mr);
}



}
}
