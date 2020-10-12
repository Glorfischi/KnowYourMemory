#include "write.hpp"

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

#include "acknowledge.hpp"



namespace kym {
namespace connection {

namespace {
  uint32_t write_buf_size = 128*1024;
  uint8_t inflight = 16;
  struct conn_details {
    WriteOpts opts; // 2 Bytes
    ringbuffer::BufferContext buffer_ctx; // 16 Bytes
    AcknowledgerContext ack_ctx; // 16 bytes
    uint32_t snd_ctx[4]; // 16 bytes
  };

  endpoint::Options write_connection_opts{
  .qp_attr = {
    .cap = {
      .max_send_wr = 200,
      .max_recv_wr = 200,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 8,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = inflight,
  .initiator_depth = inflight,
  .retry_count = 15,  
  .rnr_retry_count = 2, 
  };
}

/*
 * Write sender
 */

WriteReceiver::WriteReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack) 
  : ep_(ep), owns_ep_(true), rbuf_(rbuf), ack_(ack){
  this->length_ = write_buf_size;
  this->acked_ = 0;
  this->max_unacked_ = write_buf_size/8;

};
WriteReceiver::WriteReceiver(endpoint::Endpoint *ep, bool owns_ep, ringbuffer::Buffer *rbuf, Acknowledger *ack) 
  : ep_(ep), owns_ep_(owns_ep), rbuf_(rbuf), ack_(ack){
  this->length_ = write_buf_size;
  this->acked_ = 0;
  this->max_unacked_ = write_buf_size/8;
};

StatusOr<ReceiveRegion> WriteReceiver::Receive(){
  ReceiveRegion reg;
  volatile void *head = this->rbuf_->GetReadPtr();
  while ( *(volatile uint32_t *)head == 0){}
  uint32_t length = *(uint32_t *)head;
  reg.addr = (void *)((size_t)head + 2*sizeof(uint32_t));
  reg.length = length - 2*sizeof(uint32_t);
  reg.context = 0;

  boost::crc_32_type result;
  while (*(volatile uint32_t *)((size_t)reg.addr - sizeof(uint32_t)) != result.checksum()){
    result.reset();
    result.process_bytes((void *)(head), sizeof(uint32_t));
    result.process_bytes(reg.addr, reg.length);
  };


  this->rbuf_->Read(length);
  return reg;
}

Status WriteReceiver::Free(ReceiveRegion reg){
  uint32_t head = this->rbuf_->Free((void *)((size_t)reg.addr - 2*sizeof(uint32_t)));
  memset((void *)((size_t)reg.addr - 2*sizeof(uint32_t)), 0, reg.length + 2*sizeof(uint32_t));
  this->ack_->Ack(head);
  if ((head > this->acked_ && head - this->acked_ > this->max_unacked_)
      || (head < this->acked_ && head + (this->length_ - this->acked_) > this->max_unacked_)){

    auto stat = this->ack_->Flush();
    if (!stat.ok()){
      return stat;
    }
    this->acked_ = head;
  }

  return Status();
}


Status WriteReceiver::Close(){
  auto stat = this->rbuf_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing ringbuffer");
  }
  stat = this->ack_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing acknowledger");
  }
  if (this->owns_ep_){
    return this->ep_->Close();
  }
  return Status();
}

WriteReceiver::~WriteReceiver(){
  delete this->ack_;
  delete this->rbuf_;
  if (this->owns_ep_){
    delete this->ep_;
  }
}

StatusOr<SendRegion> WriteSender::GetMemoryRegion(size_t size){
  SendRegion reg;
  auto buf_s = this->alloc_->Alloc(size + 2*sizeof(uint32_t)); // Add 4 bytes for legth of buffer and CRC
  if (!buf_s.ok()){
    return buf_s.status();
  }
  auto buf = buf_s.value();
  *(uint32_t *)buf.addr = buf.length;
  reg.addr = (void *)((uint64_t)buf.addr + 2*sizeof(uint32_t));
  reg.length = size;
  reg.lkey = buf.lkey;
  reg.context = buf.context;
  return reg;
}

Status WriteSender::Send(SendRegion reg){
  auto id_stat = this->SendAsync(reg);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}
StatusOr<uint64_t> WriteSender::SendAsync(SendRegion reg){
  uint64_t id = this->next_id_++;
  uint32_t len = reg.length + 2*sizeof(uint32_t);
  auto addr_s = this->rbuf_->GetWriteAddr(len);
  if (!addr_s.ok()){
    auto head_s = this->ack_->Get();
    if (!head_s.ok()){
      return addr_s.status().Wrap(head_s.status().message());
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
  boost::crc_32_type result;
  result.process_bytes((void *)((size_t)reg.addr - 2*sizeof(uint32_t)), sizeof(uint32_t));
  result.process_bytes(reg.addr, reg.length);
  *(uint32_t *)((size_t)reg.addr - sizeof(uint32_t)) = result.checksum();


  auto stat = this->ep_->PostWrite(id, reg.lkey,(void *)((size_t)reg.addr - 2*sizeof(uint32_t)), 
      len, addr, this->rbuf_->GetKey());
  if (!stat.ok()){
    return stat;
  }
  
  this->rbuf_->Write(len);
  return id;
}


StatusOr<uint64_t> get_write_addr_or_die(ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack, uint32_t len){
  auto addr_s = rbuf->GetWriteAddr(len);
  if (!addr_s.ok()){
    auto head_s = ack->Get();
    if (!head_s.ok()){
      return head_s.status().Wrap("error getting new head");
    }
    rbuf->UpdateHead(head_s.value());
    addr_s = rbuf->GetWriteAddr(len);
    while (!addr_s.ok()){
      head_s = ack->Get();
      if (!head_s.ok()){
        return addr_s.status().Wrap(head_s.status().message());
      }
      rbuf->UpdateHead(head_s.value());
      addr_s = rbuf->GetWriteAddr(len);
    }
  }
  return addr_s;
}

StatusOr<uint64_t> WriteSender::SendAsync(std::vector<SendRegion> regions){
  uint64_t id = this->next_id_++;

  int i = 0;
  int batch_size = regions.size();
  ibv_send_wr wrs[batch_size];
  ibv_sge sges[batch_size];


  // TODO(fischi): We need to recover when a send fails.
  for (auto reg : regions) {
    uint32_t len = reg.length + 2*sizeof(uint32_t);
    auto addr_s = get_write_addr_or_die(this->rbuf_, this->ack_, len);
    if(!addr_s.ok()){
      return addr_s.status().Wrap("error sending batch. Might be in inconsistent state");
    }
    this->rbuf_->Write(len);

    boost::crc_32_type result;
    result.process_bytes((void *)((size_t)reg.addr - 2*sizeof(uint32_t)), sizeof(uint32_t));
    result.process_bytes(reg.addr, reg.length);
    *(uint32_t *)((size_t)reg.addr - sizeof(uint32_t)) = result.checksum();

    struct ibv_sge *sge = &sges[i];
    ibv_send_wr *wr = &wrs[i];
    bool last = (++i == batch_size);

    sge->addr = ((size_t)reg.addr - 2*sizeof(uint32_t));
    sge->length = len;
    sge->lkey =  reg.lkey;

    wr->wr_id = id;
    wr->next = last ? NULL : &(wrs[i]);
    wr->sg_list = sge;
    wr->num_sge = 1;
    wr->opcode = IBV_WR_RDMA_WRITE;
    wr->wr.rdma.remote_addr = addr_s.value();
    wr->wr.rdma.rkey = this->rbuf_->GetKey();

    wr->send_flags = last ? IBV_SEND_SIGNALED : 0;  

  }

  struct ibv_send_wr *bad;
  auto stat = this->ep_->PostSendRaw(&(wrs[0]), &bad);
  if (!stat.ok()){
    return stat;
  }
  
  return id;
}
Status WriteSender::Send(std::vector<SendRegion> regions){
  auto id_stat = this->SendAsync(regions);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}

Status WriteSender::Wait(uint64_t id){
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

Status WriteSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = (void *)((size_t)region.addr - 2*sizeof(uint32_t));
  mr.length = region.length + 2*sizeof(uint32_t);
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->alloc_->Free(mr);
}

Status WriteSender::Close(){
  auto stat = this->ack_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing acknowledger");
  }
  if (this->owns_ep_){
    return this->ep_->Close();
  }
  return Status();

}

WriteSender::~WriteSender(){
  delete this->ack_;
  delete this->rbuf_;
  if (this->owns_ep_){
    delete this->ep_;
  }
}

Status WriteConnection::Close(){
  if (this->ep_ != nullptr){
    auto stat = this->ep_->Close();
    if (!stat.ok()){
      return stat.Wrap("Error closing endpoint");
    }
  }
  auto stat = this->rcv_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing receiver");
  }
  return this->snd_->Close();
}
WriteConnection::~WriteConnection(){
  delete this->rcv_;
  delete this->snd_;
  if (this->ep_ != nullptr){
    delete this->ep_;
  }
}

/*
 * Write Offset
 */ 

Status WriteOffsetReceiver::Close() {
  auto stat = WriteReceiver::Close();
  if (!stat.ok()){
    return stat;
  }
  int ret = ibv_dereg_mr(this->tail_mr_);
  if (ret) {
    return Status(StatusCode::Internal, "error deregistering tail MR");
  }
  free(this->tail_mr_->addr);
  return Status();
};

StatusOr<ReceiveRegion> WriteOffsetReceiver::Receive(){
  ReceiveRegion reg;

  uint32_t head_off = this->rbuf_->GetReadOff();
  while(*this->tail_ == head_off){}

  void *head = this->rbuf_->GetReadPtr();
  uint32_t length = *(uint32_t *)head;
  reg.addr = (void *)((size_t)head + sizeof(uint32_t));
  reg.length = length - sizeof(uint32_t);
  reg.context = 0;
  this->rbuf_->Read(length);

  return reg;


}

Status WriteOffsetReceiver::Free(ReceiveRegion reg){
  uint32_t head = this->rbuf_->Free((void *)((char *)reg.addr - sizeof(uint32_t)));
  this->ack_->Ack(head);
  if ((head > this->acked_ && head - this->acked_ > this->max_unacked_)
      || (head < this->acked_ && head + (this->length_ - this->acked_) > this->max_unacked_)){
    auto stat = this->ack_->Flush();
    if (!stat.ok()){
      return stat;
    }
    this->acked_ = head;
  }

  return Status();
}

StatusOr<SendRegion> WriteOffsetSender::GetMemoryRegion(size_t size){
  SendRegion reg;
  auto buf_s = this->alloc_->Alloc(size + sizeof(uint32_t)); // Add 2 bytes for legth of buffer
  if (!buf_s.ok()){
    return buf_s.status();
  }
  auto buf = buf_s.value();
  *(uint32_t *)buf.addr = buf.length;
  reg.addr = (void *)((uint64_t)buf.addr + sizeof(uint32_t));
  reg.length = size;
  reg.lkey = buf.lkey;
  reg.context = buf.context;
  return reg;

}

Status WriteOffsetSender::Send(SendRegion reg){
  auto id_stat = this->SendAsync(reg);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}

StatusOr<uint64_t> WriteOffsetSender::SendAsync(SendRegion reg){
  auto id = this->next_id_++;
  uint32_t len = reg.length + sizeof(uint32_t);
  auto addr_s = get_write_addr_or_die(this->rbuf_, this->ack_, len);
  if (!addr_s.ok()){
    return addr_s.status();
  }
  uint64_t addr = addr_s.value();
  auto tail_s = this->rbuf_->GetNextTail(len);
  if (!tail_s.ok()){
    return tail_s.status();
  }
  uint32_t tail = tail_s.value();

  struct ibv_sge sge_off;
  sge_off.addr = (uintptr_t)&tail;
  sge_off.length = sizeof(uint32_t);

  struct ibv_send_wr wr_off, *bad;
  wr_off.wr_id = id;
  wr_off.next = NULL;
  wr_off.sg_list = &sge_off;
  wr_off.num_sge = 1;
  wr_off.opcode = IBV_WR_RDMA_WRITE;
  wr_off.wr.rdma.remote_addr = this->tail_addr_;
  wr_off.wr.rdma.rkey = this->tail_key_;
  wr_off.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  


  struct ibv_sge sge_data;
  sge_data.addr = (size_t)reg.addr - sizeof(uint32_t);
  sge_data.length = len;
  sge_data.lkey = reg.lkey;

  struct ibv_send_wr wr_data;
  wr_data.wr_id = id;
  wr_data.next = &wr_off;
  wr_data.sg_list = &sge_data;
  wr_data.num_sge = 1;
  wr_data.opcode = IBV_WR_RDMA_WRITE;
  wr_data.wr.rdma.remote_addr = addr;
  wr_data.wr.rdma.rkey = this->rbuf_->GetKey();
  wr_data.send_flags = 0;  

  auto stat = this->ep_->PostSendRaw(&wr_data, &bad);
  if (!stat.ok()){
    std::cerr << "bad_id " << bad->wr_id << std::endl;
    return stat;
  }
  
  this->rbuf_->Write(len);
  return id;
}

Status WriteOffsetSender::Send(std::vector<SendRegion> regions){
  auto id_stat = this->SendAsync(regions);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}
StatusOr<uint64_t> WriteOffsetSender::SendAsync(std::vector<SendRegion> regions){
  uint64_t id = this->next_id_++;

  int i = 0;
  int batch_size = regions.size();
  ibv_send_wr wrs[batch_size+1];
  ibv_sge sges[batch_size+1];


  // TODO(fischi): We need to recover when a send fails.
  for (auto reg : regions) {
    uint32_t len = reg.length + sizeof(uint32_t);
    auto addr_s = get_write_addr_or_die(this->rbuf_, this->ack_, len);
    if(!addr_s.ok()){
      return addr_s.status().Wrap("error sending batch. Might be in inconsistent state");
    }
    this->rbuf_->Write(len);

    struct ibv_sge *sge = &sges[i];
    ibv_send_wr *wr = &wrs[i];
    i++;

    sge->addr = (size_t)reg.addr - sizeof(uint32_t);
    sge->length = len;
    sge->lkey =  reg.lkey;

    wr->wr_id = i+100;
    wr->next = &(wrs[i]);
    wr->sg_list = sge;
    wr->num_sge = 1;
    wr->opcode = IBV_WR_RDMA_WRITE;
    wr->wr.rdma.remote_addr = addr_s.value();
    wr->wr.rdma.rkey = this->rbuf_->GetKey();

    wr->send_flags = 0;
  }
  auto tail = this->rbuf_->GetTail();
  sges[i].addr = (uintptr_t)&tail;
  sges[i].length = sizeof(uint32_t);

  wrs[i].wr_id = id;
  wrs[i].next = NULL;
  wrs[i].sg_list = &sges[i];
  wrs[i].num_sge = 1;
  wrs[i].opcode = IBV_WR_RDMA_WRITE;
  wrs[i].wr.rdma.remote_addr = this->tail_addr_;
  wrs[i].wr.rdma.rkey = this->tail_key_;
  wrs[i].send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  



  struct ibv_send_wr *bad;
  auto stat = this->ep_->PostSendRaw(&(wrs[0]), &bad);
  if (!stat.ok()){
    std::cerr << "bad_id " << bad->wr_id << std::endl;
    return stat;
  }
  
  return id;

}

Status WriteOffsetSender::Wait(uint64_t id){
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

Status WriteOffsetSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = (void *)((size_t)region.addr - sizeof(uint32_t));
  mr.length = region.length + sizeof(uint32_t);
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->alloc_->Free(mr);
}





/*
 * WriteImm
 */ 


Status WriteImmReceiver::Close() {
  auto stat = WriteReceiver::Close();
  if (!stat.ok()){
    return stat;
  }
  return this->rq_->Close();
};

StatusOr<ReceiveRegion> WriteImmReceiver::Receive(){
  ReceiveRegion reg;

  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status().Wrap("error polling receive cq");
  }
  struct ibv_wc wc = wcStatus.value();
  uint32_t msg_len = wc.imm_data;

  auto stat = this->rq_->PostMR(wc.wr_id);
  if (!stat.ok()){
    return stat.Wrap("error reposting mr");
  }
  void *addr = this->rbuf_->Read(msg_len);
  reg.addr = addr;
  reg.length = msg_len;
  reg.context = 0;
  return reg;
}

Status WriteImmReceiver::Free(ReceiveRegion reg){
  uint32_t head = this->rbuf_->Free(reg.addr);
  this->ack_->Ack(head);
  if ((head > this->acked_ && head - this->acked_ > this->max_unacked_)
      || (head < this->acked_ && head + (this->length_ - this->acked_) > this->max_unacked_)){
    auto stat = this->ack_->Flush();
    if (!stat.ok()){
      return stat;
    }
    this->acked_ = head;
  }

  return Status();
}

StatusOr<SendRegion> WriteImmSender::GetMemoryRegion(size_t size){
  SendRegion reg;
  auto buf_s = this->alloc_->Alloc(size); // Add 2 bytes for legth of buffer
  if (!buf_s.ok()){
    return buf_s.status();
  }
  auto buf = buf_s.value();
  reg.addr = buf.addr;
  reg.length = size;
  reg.lkey = buf.lkey;
  reg.context = buf.context;
  return reg;
}

Status WriteImmSender::Send(SendRegion reg){
  auto id_stat = this->SendAsync(reg);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}


StatusOr<uint64_t> WriteImmSender::SendAsync(SendRegion reg){
  auto id = this->next_id_++;
  uint32_t len = reg.length;
  auto addr_s = get_write_addr_or_die(this->rbuf_, this->ack_, len);
  if (!addr_s.ok()){
    return addr_s.status();
  }
  uint64_t addr = addr_s.value();

  auto stat = this->ep_->PostWriteWithImmidate(id, reg.lkey, reg.addr, len, addr, this->rbuf_->GetKey(), len);
  if (!stat.ok()){
    return stat;
  }
  this->rbuf_->Write(len);
  return id;
}

Status WriteImmSender::Send(std::vector<SendRegion> regions){
  auto id_stat = this->SendAsync(regions);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value()); 
}

StatusOr<uint64_t> WriteImmSender::SendAsync(std::vector<SendRegion> regions){
  uint64_t id = this->next_id_++;

  int i = 0;
  int batch_size = regions.size();
  ibv_send_wr wrs[batch_size];
  ibv_sge sges[batch_size];


  // TODO(fischi): We need to recover when a send fails.
  for (auto reg : regions) {
    uint32_t len = reg.length;
    auto addr_s = get_write_addr_or_die(this->rbuf_, this->ack_, len);
    if(!addr_s.ok()){
      return addr_s.status().Wrap("error sending batch. Might be in inconsistent state");
    }
    this->rbuf_->Write(len);

    struct ibv_sge *sge = &sges[i];
    ibv_send_wr *wr = &wrs[i];
    bool last = (++i == batch_size);

    sge->addr = (size_t)reg.addr;
    sge->length = len;
    sge->lkey =  reg.lkey;

    wr->wr_id = id;
    wr->next = last ? NULL : &(wrs[i]);
    wr->sg_list = sge;
    wr->num_sge = 1;
    wr->imm_data = len;
    wr->opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
    wr->wr.rdma.remote_addr = addr_s.value();
    wr->wr.rdma.rkey = this->rbuf_->GetKey();

    wr->send_flags = last ? IBV_SEND_SIGNALED : 0;  
  }

  struct ibv_send_wr *bad;
  auto stat = this->ep_->PostSendRaw(&(wrs[0]), &bad);
  if (!stat.ok()){
    return stat;
  }
  
  return id;
}

Status WriteImmSender::Wait(uint64_t id){
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

Status WriteImmSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->alloc_->Free(mr);
}



/*
 *
 *  CONNECTION SETUP
 *
 */
Status initReceiver(struct ibv_pd *pd, WriteOpts opts, ringbuffer::Buffer **rbuf, Acknowledger **ack, struct ibv_mr **metadata){
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      {
        auto ack_s = GetReadAcknowledger(pd);
        if (!ack_s.ok()){
          return ack_s.status();
        }
        *ack = ack_s.value();
        break;
      }
      break;
    case kAcknowledgerSend:
      {
        *ack = new SendAcknowledger(); // Placeholder
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  switch (opts.buffer) {
    case kBufferBasic:
      return Status(StatusCode::NotImplemented, "basic buffer not implemented");
      break;
    case kBufferMagic:
      {
        auto rbuf_s = ringbuffer::NewMagicRingBuffer(pd, write_buf_size); 
        if (!rbuf_s.ok()){
          return rbuf_s.status().Wrap("error setting up magic receive buffer");
        }
        *rbuf = rbuf_s.value();
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown buffer option");
  }
  if (opts.sender == kSenderWriteOffset){
    void *buf = calloc(1, sizeof(uint32_t));
    *metadata = ibv_reg_mr(pd, buf, sizeof(uint32_t), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (*metadata == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering MR");
    }
  }
  return Status();
}

Status connectReceiver(endpoint::Endpoint * ep, conn_details det, ringbuffer::Buffer **rbuf, Acknowledger **ack){
  auto opts = det.opts;
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      break;
    case kAcknowledgerSend:
      {
        auto sa_s = GetSendAcknowledger(ep);
        if (!sa_s.ok()){
          return sa_s.status().Wrap("error setting up send acknowledger");
        }
        *ack = sa_s.value();
      }
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  return Status();
}




Status initSender(ibv_pd *pd, WriteOpts opts, memory::Allocator **alloc){
  switch (opts.allocator) {
    case kAllocatorDumb:
      *alloc = new memory::DumbAllocator(pd);
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown allocator option");
  }
  return Status();
}

Status connectSender(endpoint::Endpoint * ep, conn_details det, ringbuffer::RemoteBuffer **rbuf, AckReceiver **ack){
  auto opts = det.opts;
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      {
        auto ack_s = GetReadAckReceiver(ep, det.ack_ctx);
        if (!ack_s.ok()){
          return ack_s.status();
        }
        *ack = ack_s.value();
        break;
      }
    case kAcknowledgerSend:
      {
        auto sa_s = GetSendAckReceiver(ep);
        if (!sa_s.ok()){
          return sa_s.status().Wrap("error setting up send acknowledger");
        }
        *ack = sa_s.value();
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  switch (opts.buffer) {
    case kBufferBasic:
      return Status(StatusCode::NotImplemented, "basic buffer not implemented");
      break;
    case kBufferMagic:
      {
        *rbuf = new ringbuffer::MagicRemoteBuffer(det.buffer_ctx);
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown buffer option");
  }
  return Status();
}

/*
 *
 * LISTENER
 *
 */

StatusOr<WriteListener*> ListenWrite(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  auto ln_stat = endpoint::Listen(ip, port);
  if (!ln_stat.ok()){
    return ln_stat.status();
  }
  endpoint::Listener *ln = ln_stat.value();
  return new WriteListener(ln);
}

Status WriteListener::Close(){
  return this->listener_->Close();
}

WriteListener::~WriteListener(){
  delete this->listener_;
}


StatusOr<WriteSender *> WriteListener::AcceptSender(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  memory::Allocator *alloc;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  
  auto stat = initSender(this->listener_->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  AckReceiver *ack;
  ringbuffer::RemoteBuffer *rbuf;

  stat = connectSender(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up connection details");
  }
  switch (opts.sender) {
    case kSenderWrite:
      return new WriteSender(ep, alloc, rbuf, ack);
    case kSenderWriteImm:
      return new WriteImmSender(ep, alloc, rbuf, ack);
    case kSenderWriteOffset:
      return new WriteOffsetSender(ep, alloc, rbuf, ack, *(uint64_t *)remote_conn_details->snd_ctx, remote_conn_details->snd_ctx[2]);
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }


}

StatusOr<WriteReceiver *> WriteListener::AcceptReceiver(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;
  struct ibv_mr *metadata = 0;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto stat = initReceiver(this->listener_->GetPdP(), opts, &rbuf, &ack, &metadata);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing connection details");
  }
  
  if (metadata != nullptr){
    *(uint64_t *)local_conn_details.snd_ctx = (uintptr_t)metadata->addr;
    local_conn_details.snd_ctx[2] = metadata->lkey;
  }
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();

  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }


  switch (opts.sender) {
    case kSenderWrite:
      return new WriteReceiver(ep, rbuf, ack);
    case kSenderWriteImm:
      {
        auto rq_s = endpoint::GetReceiveQueue(ep, 8, inflight); 
        if (!rq_s.ok()){
          return rq_s.status().Wrap("error setting up receive queue");
        }
        return new WriteImmReceiver(ep, rbuf, ack, rq_s.value());
      }
    case kSenderWriteOffset:
      {
      return new WriteOffsetReceiver(ep, rbuf, ack, metadata);
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }
}

StatusOr<WriteConnection *> WriteListener::AcceptConnection(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  if (opts.sender == kSenderWriteImm && opts.acknowledger == kAcknowledgerSend) {
      return Status(StatusCode::InvalidArgument, "duplex connection with WriteImm and Send Acknowledger not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;
  struct ibv_mr *metadata = 0;


  memory::Allocator *alloc;

  auto stat = initReceiver(this->listener_->GetPdP(), opts, &rbuf, &ack, &metadata);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing receiver details");
  }
  stat = initSender(this->listener_->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_details local_conn_details;
  local_conn_details.opts = opts;
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();
  if (metadata != nullptr){
    *(uint64_t *)local_conn_details.snd_ctx = (uintptr_t)metadata->addr;
    local_conn_details.snd_ctx[2] = metadata->lkey;
  }

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  WriteReceiver *rcvr; 
  switch (opts.sender) {
    case kSenderWrite:
      {
        rcvr = new WriteReceiver(ep, false, rbuf, ack);
        break;
      }
    case kSenderWriteImm:
      {
        auto rq_s = endpoint::GetReceiveQueue(ep, 8, inflight); 
        if (!rq_s.ok()){
          return rq_s.status().Wrap("error setting up receive queue");
        }
        rcvr = new WriteImmReceiver(ep, false, rbuf, ack, rq_s.value());
        break;
      }
    case kSenderWriteOffset:
      {
        rcvr = new WriteOffsetReceiver(ep, false, rbuf, ack, metadata);
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }

  AckReceiver *ackRcv;
  ringbuffer::RemoteBuffer *remote_rbuf;

  stat = connectSender(ep, *remote_conn_details, &remote_rbuf, &ackRcv);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up sender details");
  }
  

  WriteSender *sndr;
  switch (opts.sender) {
    case kSenderWrite:
      {
        sndr = new WriteSender(ep, false, alloc, remote_rbuf, ackRcv);
        break;
      }
    case kSenderWriteImm:
      {
        sndr = new WriteImmSender(ep, false, alloc, remote_rbuf, ackRcv);
        break;
      }
    case kSenderWriteOffset:
      {
        sndr = new WriteOffsetSender(ep, false, alloc, remote_rbuf, ackRcv, *(uint64_t *)remote_conn_details->snd_ctx, remote_conn_details->snd_ctx[2]);
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }

  return new WriteConnection(ep, sndr, rcvr);
}



/*
 *
 * DIALING
 *
 */


StatusOr<WriteSender*> DialWriteSender(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  memory::Allocator *alloc;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  auto stat = initSender(ep->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
   
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  AckReceiver *ack;
  ringbuffer::RemoteBuffer *rbuf;

  stat = connectSender(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up connection details");
  }
  switch (opts.sender) {
    case kSenderWrite:
      return new WriteSender(ep, alloc, rbuf, ack);
    case kSenderWriteImm:
      return new WriteImmSender(ep, alloc, rbuf, ack);
    case kSenderWriteOffset:
      return new WriteOffsetSender(ep, alloc, rbuf, ack, *(uint64_t *)remote_conn_details->snd_ctx, remote_conn_details->snd_ctx[2]);
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }
}


StatusOr<WriteReceiver*> DialWriteReceiver(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;
  struct ibv_mr *metadata = 0;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  auto stat = initReceiver(ep->GetPdP(), opts, &rbuf, &ack, &metadata);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing connection details");
  }
  
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();
  if (metadata != nullptr){
    *(uint64_t *)local_conn_details.snd_ctx = (uintptr_t)metadata->addr;
    local_conn_details.snd_ctx[2] = metadata->lkey;
  }

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  } 
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }
  switch (opts.sender) {
    case kSenderWrite:
      return new WriteReceiver(ep, rbuf, ack);
    case kSenderWriteOffset:
      return new WriteOffsetReceiver(ep, rbuf, ack, metadata);
    case kSenderWriteImm:
      {
        auto rq_s = endpoint::GetReceiveQueue(ep, 8, inflight); 
        if (!rq_s.ok()){
          return rq_s.status().Wrap("error setting up receive queue");
        }
        return new WriteImmReceiver(ep, rbuf, ack, rq_s.value());
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }
}


StatusOr<WriteConnection*> DialWriteConnection(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  if (opts.sender == kSenderWriteImm && opts.acknowledger == kAcknowledgerSend) {
      return Status(StatusCode::InvalidArgument, "duplex connection with WriteImm and Send Acknowledger not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;
  struct ibv_mr *metadata = 0;

  memory::Allocator *alloc;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();

  auto stat = initReceiver(ep->GetPdP(), opts, &rbuf, &ack, &metadata);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing receiver details");
  }
  stat = initSender(ep->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_details local_conn_details;
  local_conn_details.opts = opts;
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();
  if (metadata != nullptr){
    *(uint64_t *)local_conn_details.snd_ctx = (uintptr_t)metadata->addr;
    local_conn_details.snd_ctx[2] = metadata->lkey;
  }

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
    
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  WriteReceiver *rcvr;
  switch (opts.sender) {
    case kSenderWrite:
      {
        rcvr = new WriteReceiver(ep, false, rbuf, ack);
        break;
      }
    case kSenderWriteImm:
      {
        auto rq_s = endpoint::GetReceiveQueue(ep, 8, inflight); 
        if (!rq_s.ok()){
          return rq_s.status().Wrap("error setting up receive queue");
        }
        rcvr = new WriteImmReceiver(ep, false, rbuf, ack, rq_s.value());
        break;
      }
    case kSenderWriteOffset:
      {
        rcvr = new WriteOffsetReceiver(ep, false, rbuf, ack, metadata);
        break;
      }
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }

  AckReceiver *ackRcv;
  ringbuffer::RemoteBuffer *remote_rbuf;

  stat = connectSender(ep, *remote_conn_details, &remote_rbuf, &ackRcv);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up sender details");
  }
 

  WriteSender *sndr;
  switch (opts.sender) {
    case kSenderWrite:
      sndr = new WriteSender(ep, false, alloc, remote_rbuf, ackRcv);
      break;
    case kSenderWriteImm:
      sndr = new WriteImmSender(ep, false, alloc, remote_rbuf, ackRcv);
      break;
    case kSenderWriteOffset:
      sndr = new WriteOffsetSender(ep, false, alloc, remote_rbuf, ackRcv, *(uint64_t *)remote_conn_details->snd_ctx, remote_conn_details->snd_ctx[2]);
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown sender option");
  }

  return new WriteConnection(ep, sndr, rcvr);

}

}
}
