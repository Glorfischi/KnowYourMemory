#include "write_atomic.hpp"

#include <cstdint>
#include <cstdlib>
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
const uint32_t write_atomic_outstanding = 30;
const size_t write_atomic_buf_size = 10*1024*1024;

namespace {
  struct connectionInfo {
    uint64_t buf_addr;
    uint32_t buf_key;
    uint64_t meta_addr;
    uint32_t meta_key;
  };

  endpoint::Options default_write_atomic_options = {
    .qp_attr = {
      .cap = {
        .max_send_wr = write_atomic_outstanding,
        .max_recv_wr = write_atomic_outstanding,
        .max_send_sge = 1,
        .max_recv_sge = 1,
        .max_inline_data = 0,
      },
      .qp_type = IBV_QPT_RC,
    },
    .use_srq = true,
    .responder_resources = write_atomic_outstanding,
    .initiator_depth =  write_atomic_outstanding,
    .retry_count = 5,  
    .rnr_retry_count = 2, 
  };

}


StatusOr<std::unique_ptr<WriteAtomicSender>> DialWriteAtomic(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  auto ep_stat = kym::endpoint::Dial(ip, port, default_write_atomic_options);
  if (!ep_stat.ok()){
    return ep_stat.status();
  }
  std::unique_ptr<endpoint::Endpoint> ep = ep_stat.value();

  auto alloc = std::make_shared<memory::DumbAllocator>(ep->GetPd());

  auto pd = ep->GetPd();
  struct write_atomic_meta *rbuf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *rbuf_meta_mr = ibv_reg_mr(&pd, rbuf_meta, sizeof(struct write_atomic_meta), IBV_ACCESS_LOCAL_WRITE);  

  connectionInfo *ci;
  ep->GetConnectionInfo((void **)&ci);

  auto conn = std::make_unique<WriteAtomicSender>(std::move(ep), alloc, ci->buf_addr, ci->buf_key, ci->meta_addr, ci->meta_key, rbuf_meta_mr);

  return std::move(conn);
}

StatusOr<std::unique_ptr<WriteAtomicListener>> ListenWriteAtomic(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  auto ln_stat = endpoint::Listen(ip, port);
  if (!ln_stat.ok()){
    return ln_stat.status();
  }
  std::unique_ptr<endpoint::Listener> ln = ln_stat.value();

  auto magic_stat = ringbuffer::GetMagicBuffer(write_atomic_buf_size);
  if (!magic_stat.ok()){
    return magic_stat.status().Wrap("error initialzing magic buffer while accepting");
  }
  auto pd = ln->GetPd();
  auto buf_mr = ibv_reg_mr(&pd, magic_stat.value(), 2 * write_atomic_buf_size, 
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  auto buf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  auto buf_meta_mr = ibv_reg_mr(&pd, buf_meta, sizeof(struct write_atomic_meta),
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC);  
  auto rcv_buf = (char *)calloc(write_atomic_outstanding, sizeof(char));
  auto rcv_mr = ibv_reg_mr(&pd, rcv_buf, write_atomic_outstanding * sizeof(char),
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

  return std::make_unique<WriteAtomicListener>(std::move(ln), buf_mr, buf_meta_mr, rcv_mr, write_atomic_outstanding);
}


WriteAtomicListener::WriteAtomicListener(std::unique_ptr<endpoint::Listener> listener, struct ibv_mr *buf_mr, 
    struct ibv_mr *buf_meta_mr, struct ibv_mr *rcv_mr, uint32_t rcv_bufs_count ):listener_(std::move(listener)){
  this->buf_mr_ = buf_mr;
  this->buf_size_ = buf_mr->length/2;

  this->buf_meta_mr_ = buf_meta_mr;
  this->buf_meta_ = (struct write_atomic_meta *)buf_meta_mr->addr;

  this->rcv_mr_ = rcv_mr;
  this->rcv_bufs_count_ = rcv_bufs_count;
  this->rcv_bufs_ = (char *)rcv_mr->addr;

}

Status WriteAtomicListener::Close(){
  //TODO(fischi) Free
  return this->listener_->Close();
}
Status WriteAtomicListener::Accept(){
  auto opts = default_write_atomic_options;

  connectionInfo local_ci;
  local_ci.buf_addr = (uint64_t)this->buf_mr_->addr;
  local_ci.buf_key = this->buf_mr_->lkey;
  local_ci.meta_addr = (uint64_t)this->buf_meta_;
  local_ci.meta_key = this->buf_meta_mr_->lkey;
  
  opts.private_data = &local_ci;
  opts.private_data_len = sizeof(local_ci);

  // If this is not the first endpoint we share the rq and recv cq
  if (this->eps_.size() > 0){
    opts.qp_attr.srq = this->eps_[0]->GetSRQ();
    opts.qp_attr.recv_cq = this->eps_[0]->GetRecvCQ();
  }

  auto epStatus = this->listener_->Accept(opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::unique_ptr<endpoint::Endpoint> ep = epStatus.value();
  // If this is the first endpoint we share the rq and recv cq
  // FIXME(Fischi) This is uggly. We need to think about if we could integrate CQ management into the endpoint abstraction
  if (this->eps_.size() == 0){
    for (int i = 0; i<this->rcv_bufs_count_; i++){
      auto stat = ep->PostRecv(i, this->rcv_mr_->lkey, this->rcv_bufs_ + i, 1); 
      if (!stat.ok()){
        return stat.Wrap("error initialzing receive buffers");
      }
    }
    
  }
  this->eps_.push_back(std::move(ep));

  return Status();
}
StatusOr<ReceiveRegion> WriteAtomicListener::Receive(){
  auto wcStatus = this->eps_[0]->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status().Wrap("error polling receive cq");
  }
  struct ibv_wc wc = wcStatus.value();

  auto status = this->eps_[0]->PostRecv(wc.wr_id, this->rcv_mr_->lkey, this->rcv_bufs_ + wc.wr_id, 1); 
  if (!status.ok()){
    return status;
  }

  // TODO(Fischi) out of order reading.
  uint32_t read_offset = this->buf_meta_->head % this->buf_size_;
  ReceiveRegion reg;
  reg.addr = (void *)((uint64_t)this->buf_mr_->addr + read_offset);
  reg.length = wc.byte_len;
  return reg;
}
Status WriteAtomicListener::Free(ReceiveRegion reg){
  this->buf_meta_->head  =  (this->buf_meta_->head + reg.length);
  return Status();
}




WriteAtomicSender::WriteAtomicSender(std::unique_ptr<endpoint::Endpoint> ep, std::shared_ptr<memory::Allocator> allocator,
    uint64_t rbuf_vaddr, uint32_t rbuf_rkey,  uint64_t rbuf_meta_vaddr, uint32_t rbuf_meta_rkey, struct ibv_mr *rbuf_meta_mr){
  this->ep_ = std::move(ep);
  this->allocator_ = allocator;

  this->rbuf_vaddr_ = rbuf_vaddr;
  this->rbuf_rkey_ = rbuf_rkey;

  this->rbuf_meta_vaddr_ = rbuf_meta_vaddr;
  this->rbuf_meta_rkey_ = rbuf_meta_rkey;
  this->rbuf_meta_mr_ = rbuf_meta_mr;
  this->rbuf_meta_ = (struct write_atomic_meta *)rbuf_meta_mr->addr;

  this->rbuf_size_ = write_atomic_buf_size;
}

Status WriteAtomicSender::Close(){
  int ret = ibv_dereg_mr(this->rbuf_meta_mr_);
  if (ret){
    return Status(StatusCode::Internal, "error " + std::to_string(ret) + " deregistering remote head mr");
  }
  free(rbuf_meta_mr_->addr);
  return this->ep_->Close();
}

StatusOr<SendRegion> WriteAtomicSender::GetMemoryRegion(size_t size){
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
}

Status WriteAtomicSender::Send(SendRegion region){
  if(region.length >= this->rbuf_size_){
    return Status(StatusCode::InvalidArgument, "message too large");
  }

  //TODO(fischi) Get and set tail using fetch and add
  auto fetch_stat = this->ep_->PostFetchAndAdd(region.context, region.length, this->rbuf_meta_mr_->lkey, 
      &this->rbuf_meta_->tail, (uint64_t)&((struct write_atomic_meta *)this->rbuf_meta_vaddr_)->tail, this->rbuf_meta_rkey_);
  if (!fetch_stat.ok()){
    return fetch_stat;
  }
  auto wc_stat = this->ep_->PollSendCq();
  if (!wc_stat.ok()){
    return wc_stat.status().Wrap("error during fetch and add");
  }

  // FIXME(Fischi) We should NEVER fail in here. If we do the buffer is in an inconsistent state

  while(this->rbuf_meta_->head + this->rbuf_size_ <= this->rbuf_meta_->tail + region.length){
    auto read_stat = this->ep_->PostRead(region.context, this->rbuf_meta_mr_->lkey, &this->rbuf_meta_->head, sizeof(this->rbuf_meta_->head),
        (uint64_t)&((struct write_atomic_meta *)this->rbuf_meta_vaddr_)->head, this->rbuf_meta_rkey_);
    if (!fetch_stat.ok()){
      return fetch_stat;
    }
    auto wc_stat = this->ep_->PollSendCq();
    if (!wc_stat.ok()){
      return wc_stat.status().Wrap("error during reading head position");
    }
    // TODO(fischi) Maybe add some kind of backoff?
  }

  uint64_t write_off = this->rbuf_meta_->tail % this->rbuf_size_;

  auto send_stat = this->ep_->PostWriteWithImmidate(1, region.lkey, region.addr, region.length, 
      this->rbuf_vaddr_ + write_off, this->rbuf_rkey_, 0);
  if (!send_stat.ok()){
    return send_stat;
  }
  wc_stat = this->ep_->PollSendCq();
  if (!wc_stat.ok()){
    return wc_stat.status();
  }
  return Status();
}

Status WriteAtomicSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}


}
}
