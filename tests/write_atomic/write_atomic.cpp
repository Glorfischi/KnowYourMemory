#include "write_atomic.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <ostream>
#include <stddef.h>
#include <string>
#include <vector>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"

#include "ring_buffer/magic_buffer.hpp"

#include "conn.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"
#include "shared_receive_queue.hpp"

#include "debug.h"

namespace kym {
namespace connection {

const uint32_t write_atomic_outstanding = 30;

namespace {
  struct connectionInfo {
    uint64_t buf_addr;
    uint32_t buf_key;
    uint32_t buf_size;
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
    .responder_resources = 15,
    .initiator_depth =  15,
    .retry_count = 5,  
    .rnr_retry_count = 2, 
    .native_qp = false,
    .inline_recv = 0,
  };

}



Status WriteAtomicInstance::Init(struct ibv_context *ctx, struct ibv_pd *pd){
  this->pd_ = pd;
  this->ctx_ = ctx;
  auto magic_stat = ringbuffer::GetMagicBuffer(this->buf_size_);
  if (!magic_stat.ok()){
    return magic_stat.status().Wrap("error initialzing magic buffer");
  }
  this->buf_mr_ = ibv_reg_mr(pd, magic_stat.value(), 2 * this->buf_size_, 
        IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  this->buf_meta_ =  (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));

  if (this->use_dm_) {
    struct ibv_alloc_dm_attr attr = {};
    attr.length = sizeof(struct write_atomic_meta);
    this->buf_meta_dm_ = ibv_alloc_dm(ctx, &attr);
    if (this->buf_meta_dm_ == nullptr) {
      return Status(StatusCode::Internal, "Error registering device memory ");
    }
    this->buf_meta_mr_ = ibv_reg_dm_mr(pd, this->buf_meta_dm_, 0, sizeof(struct write_atomic_meta),
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_ZERO_BASED 
        | IBV_ACCESS_REMOTE_ATOMIC);
    if (this->buf_meta_mr_ == nullptr) {
      return Status(StatusCode::Internal, "Error registering device memory MR");
    }
    int ret = ibv_memcpy_to_dm(this->buf_meta_dm_, 0, this->buf_meta_, sizeof(struct write_atomic_meta));
    if (ret) {
      return Status(StatusCode::Internal, "Error initialzing device memory");
    }
  } else {
    this->buf_meta_mr_ = ibv_reg_mr(pd, (void *)this->buf_meta_, sizeof(struct write_atomic_meta),
        IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC);  
  }

  // TODO(fischi): Parameter
  auto srq_s = endpoint::GetSharedReceiveQueue(pd, 1, 100);
  if (!srq_s.ok()){
    return srq_s.status().Wrap("error getting srq");
  }
  this->srq_ = srq_s.value();

  // TODO(fischi): Parameter
  this->rcv_cq_ = ibv_create_cq(ctx, 100, NULL, NULL, 0 );
  
  debug(stderr, "Initialized instance with pd %p | srq %p\n", this->pd_, this->srq_->GetSRQ());
  return Status();
}
WriteAtomicInstance::~WriteAtomicInstance(){
  free(this->buf_meta_);
  
}
Status WriteAtomicInstance::Close(){
  ibv_dereg_mr(this->buf_mr_);
  ibv_dereg_mr(this->buf_meta_mr_);
  if (this->use_dm_) {
    ibv_free_dm(this->buf_meta_dm_);
  }
  return ringbuffer::FreeMagicBuffer(this->buf_mr_->addr, this->buf_size_);
}

Status WriteAtomicInstance::Listen(std::string ip, int port){
  if (this->listener_ != nullptr) {
    return Status(StatusCode::InvalidArgument, "the instance is already listening");
  }
  if (this->pd_ != nullptr) {
    // TODO(fischi): We could probably extend the listener to take an optinal pd. o/w if we first dial to an endpoint and 
    // then listen the pd will (probably) not match
    return Status(StatusCode::NotImplemented, "listening after dial is not supported");

  }
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  auto ln_stat = endpoint::Listen(ip, port);
  if (!ln_stat.ok()){
    return ln_stat.status();
  }
  endpoint::Listener *ln = ln_stat.value();
  this->listener_ = ln;

  return this->Init(ln->GetContext(), ln->GetPd()).Wrap("error initalizing instance");
}


StatusOr<WriteAtomicConnection *> WriteAtomicInstance::Accept(write_atomic_conn_opts opts) {
  if (this->listener_ == nullptr) {
    return Status(StatusCode::InvalidArgument, "the instance is not listening");
  }
  auto conn_opts = default_write_atomic_options;

  connectionInfo local_ci;
  local_ci.buf_addr = (uint64_t)this->buf_mr_->addr;
  local_ci.buf_key = this->buf_mr_->lkey;
  local_ci.buf_size = this->buf_size_;
  if (this->use_dm_ ) {
    local_ci.meta_addr = 0; // DM is zero indexed
  } else {
    local_ci.meta_addr = (uint64_t)this->buf_meta_;
  }
  local_ci.meta_key = this->buf_meta_mr_->lkey;
  
  conn_opts.private_data = &local_ci;
  conn_opts.private_data_len = sizeof(local_ci);

  conn_opts.qp_attr.srq = this->srq_->GetSRQ();
  // TODO(Fischi) extend srq through ibv_modify_srq

  if (!opts.standalone_receive) {
    conn_opts.qp_attr.recv_cq = this->rcv_cq_;
  }

  auto epStatus = this->listener_->Accept(conn_opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();
  connectionInfo *remote_ci;
  ep->GetConnectionInfo((void **)&remote_ci);
  auto allocator = new memory::DumbAllocator(ep->GetPd());
  struct write_atomic_meta *rbuf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *rbuf_meta_mr = ibv_reg_mr(ep->GetPd(), rbuf_meta, sizeof(struct write_atomic_meta), IBV_ACCESS_LOCAL_WRITE);  
  WriteAtomicConnection *conn = new WriteAtomicConnection(ep, allocator,
      remote_ci->buf_addr, remote_ci->buf_key, remote_ci->buf_size,
      remote_ci->meta_addr, remote_ci->meta_key, rbuf_meta_mr);
  this->conns_.push_back(conn);

  return conn;
}

StatusOr<WriteAtomicConnection *> WriteAtomicInstance::Dial(std::string ip, int port, write_atomic_conn_opts opts) {
  auto conn_opts = default_write_atomic_options;
  conn_opts.pd = this->pd_;
  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status().Wrap("error creating ep");
  }
  auto ep = ep_s.value();
  if (this->pd_ == nullptr) {
    // We have not use the pd of the ep
    auto stat = this->Init(ep->GetContext(), ep->GetPd());
  }

  connectionInfo local_ci;
  local_ci.buf_addr = (uint64_t)this->buf_mr_->addr;
  local_ci.buf_key = this->buf_mr_->lkey;
  local_ci.buf_size = this->buf_size_;
  if (this->use_dm_ ) {
    local_ci.meta_addr = 0; // DM is zero indexed
  } else {
    local_ci.meta_addr = (uint64_t)this->buf_meta_;
  }
  local_ci.meta_key = this->buf_meta_mr_->lkey;
  
  conn_opts.private_data = &local_ci;
  conn_opts.private_data_len = sizeof(local_ci);

  conn_opts.qp_attr.srq = this->srq_->GetSRQ();
  // TODO(Fischi) extend srq through ibv_modify_srq

  if (!opts.standalone_receive) {
    conn_opts.qp_attr.recv_cq = this->rcv_cq_;
  }

  auto stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("Error connecting to remote");
  }
  connectionInfo *remote_ci;
  ep->GetConnectionInfo((void **)&remote_ci);
  auto allocator = new memory::DumbAllocator(ep->GetPd());
  struct write_atomic_meta *rbuf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *rbuf_meta_mr = ibv_reg_mr(ep->GetPd(), rbuf_meta, sizeof(struct write_atomic_meta), IBV_ACCESS_LOCAL_WRITE);  
  WriteAtomicConnection *conn = new WriteAtomicConnection(ep, allocator,
      remote_ci->buf_addr, remote_ci->buf_key, remote_ci->buf_size,
      remote_ci->meta_addr, remote_ci->meta_key, rbuf_meta_mr);
  this->conns_.push_back(conn);

  return conn;
}


StatusOr<ReceiveRegion> WriteAtomicInstance::Receive(){
  struct ibv_wc wc;
  while(ibv_poll_cq(this->rcv_cq_, 1, &wc) == 0){}
  if (wc.status){
    return Status(StatusCode::Internal, "error " + std::to_string(wc.status) +  " polling recv cq\n" + std::string(ibv_wc_status_str(wc.status)));
  }
  auto stat = this->srq_->PostMR(wc.wr_id);
  if (!stat.ok()){
    return stat.Wrap("error reposing receive buffer");
  }

  int32_t local_off = wc.imm_data;
  // Let's convert the local 32 bit offset to a unique 64 bit offset 
  int32_t local_head = this->buf_meta_->head % this->buf_size_;
  int64_t global_off;
  if (local_head <= local_off) {
    // global head is in the same "buffer interval" as the global offset
    global_off = (this->buf_meta_->head/this->buf_size_)*this->buf_size_ + local_off;
  } else {
    // the global head is in the previous "interval"
    global_off = (this->buf_meta_->head/this->buf_size_ + 1)*this->buf_size_ + local_off;
  }
  ReceiveRegion reg;
  reg.context = global_off;
  reg.addr = (void *)((uint64_t)this->buf_mr_->addr + local_off);
  reg.length = wc.byte_len;
  return reg;
}
Status WriteAtomicInstance::Free(ReceiveRegion reg){
  // TODO(fischi) Locks?
  uint64_t global_start = reg.context;  // the beginning of the to be freed buffer as a unique global offset
  uint64_t global_end = global_start+reg.length; // the end of the freed buffer as a unique global offset
  // freed_regions is a hashmap that maps the start of a freed region to the end of it
  // if there exists a freed region that starts at the end of the region we currently freeing we merge them
  while (this->freed_regions_.count(global_end)){
    uint64_t tmp = this->freed_regions_[global_end];
    this->freed_regions_.erase(global_end);
    global_end = tmp;
  }
  // If the region we are freeing is the head, we update it otherwise add the regions to the freed_regions map
  if (global_start == this->buf_meta_->head){ 
    this->buf_meta_->head = global_end;
    if (this->use_dm_) {
      // The receiver is authorative of the head field. We need to reconcile the version in device memory
      // This depends on the head being the first field.
      int ret = ibv_memcpy_to_dm(this->buf_meta_dm_, 0, &this->buf_meta_->head, sizeof(uint64_t));
      if (ret) {
        return Status(StatusCode::Internal, "Error initialzing device memory");
      }
    }
  } else {
    this->freed_regions_[global_start] = global_end;
  }
  return Status();
}


WriteAtomicConnection::~WriteAtomicConnection(){
  //TODO(Fischi)
}

Status WriteAtomicConnection::Close(){
  //TODO(Fischi)
  return Status(StatusCode::NotImplemented);
}

StatusOr<SendRegion> WriteAtomicConnection::GetMemoryRegion(size_t size){
  assert(this->allocator_);
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
Status WriteAtomicConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}
Status WriteAtomicConnection::Send(SendRegion region){
  auto id_stat = this->SendAsync(region);
  if (!id_stat.ok()){
    return id_stat.status();
  }
  return this->Wait(id_stat.value());
}

StatusOr<uint64_t> WriteAtomicConnection::SendAsync(SendRegion region){
  if(region.length >= this->rbuf_size_){
    return Status(StatusCode::InvalidArgument, "message too large");
  }

  // We add the length of our message to the remote tail. We get the tail before our addition, so this is our (global)
  // write address.
  // We talk about global and local offsets. Global means 64 bit address that we wraps around. The local address is 32 bit
  // and equal to g_off % buf_len. The global offset is unique.
  uint64_t id = this->next_id_++;
  auto fetch_stat = this->ep_->PostFetchAndAdd(id, region.length, this->rbuf_meta_mr_->lkey, 
      &this->rbuf_meta_->tail, (uint64_t)&((struct write_atomic_meta *)this->rbuf_meta_vaddr_)->tail, this->rbuf_meta_rkey_);
  if (!fetch_stat.ok()){
    return fetch_stat;
  }
  // TODO(Fischi) To get any reasonable single threaded performance, we would need to be able to pipline the F&A.
  // But this would require a major refactor and interface change
  auto stat = this->Wait(id);
  if (!stat.ok()){
    return stat;
  }

  // FIXME(Fischi) We should NEVER fail in here. If we do the buffer is in an inconsistent state

  // We check if there is space to write. We essentiall reserve space without knowing if we can actually write to it.
  int rnr_i = 0;
  while(this->rbuf_meta_->head + this->rbuf_size_ <= this->rbuf_meta_->tail + region.length){
    uint64_t id = this->next_id_++;
    auto read_stat = this->ep_->PostRead(id, this->rbuf_meta_mr_->lkey, &this->rbuf_meta_->head, sizeof(this->rbuf_meta_->head),
        (uint64_t)&((struct write_atomic_meta *)this->rbuf_meta_vaddr_)->head, this->rbuf_meta_rkey_);
    if (!fetch_stat.ok()){
      return fetch_stat;
    }
    auto stat = this->Wait(id);
    if (!stat.ok()){
      return stat;
    }
    rnr_i++;
    //if (rnr_i > 100) info(stderr, "Waiting for head to update %d, local cache %ld, tail at %ld\n", rnr_i, this->rbuf_meta_->head, this->rbuf_meta_->tail);
    // TODO(fischi) Maybe add some kind of backoff?
  }

  uint64_t local_off = this->rbuf_meta_->tail % this->rbuf_size_; // We calculate the local offset to know where to write to.

  id = this->next_id_++;
  auto send_stat = this->ep_->PostWriteWithImmidate(id, region.lkey, region.addr, region.length, 
      this->rbuf_vaddr_ + local_off, this->rbuf_rkey_, local_off);
  if (!send_stat.ok()){
    return send_stat;
  }
  return id; 

}
Status WriteAtomicConnection::Wait(uint64_t id){
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


StatusOr<ReceiveRegion> WriteAtomicConnection::Receive(){
  //TODO(Fischi)
  return Status(StatusCode::NotImplemented);
}
Status WriteAtomicConnection::Free(ReceiveRegion){
  //TODO(Fischi)
  return Status(StatusCode::NotImplemented);
}



}
}
