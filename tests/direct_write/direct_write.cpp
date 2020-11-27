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

#include <thread>
#include <chrono>


#include "debug.h"

namespace kym {
namespace connection {
namespace {

const uint64_t inflight = 30;

endpoint::Options default_opts = {
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
  .responder_resources = 10,
  .initiator_depth =  10,
  .flow_control = 0,
  .retry_count = 0,  
  .rnr_retry_count = 0, 
  .native_qp = false,
  .inline_recv = 0,
};

inline uint32_t *getLengthAddr(void *addr, uint32_t max_len){
  uint64_t rawptr = (uint64_t)addr;
  return (uint32_t *)(rawptr + max_len - sizeof(uint32_t));
}

struct conn_details {
  uint64_t buf_addr;
  uint32_t buf_key;
};

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
  debug(stderr, "Sending: Getting buffer [id: %d, addr: %p, buffaddr: %p valid: %d]\n", id, &this->target_buf_[id%this->nr_buffers_], (void*)buf.addr, buf.valid);
  if (!buf.valid){
    return Status(StatusCode::RateLimit, "No receive buffer ready");
  }
  uint64_t addr = buf.addr;
  uint32_t rkey = buf.rkey;
  // We will send the complete buffer everytime.. That might be far too slow
  
  debug(stderr, "Sending: Writing [id: %d, destaddr: %p, end: %p]\n", id, (void *)addr, getLengthAddr((void *)addr, this->buf_size_));
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

  // FIXME(Fischi) The indexing of the target buffer and receive buffer is somehow out of sync
  debug(stderr, "Receive: Polling [msg_len_ptr: %p, addr: %p,  head: %d ]\n", msg_len, addr, this->rcv_head_);
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

  debug(stderr, "Freeing buffer [tail: %d, addr: %p, baddr: %p ]\n", this->r_rcv_tail_, (void*) this->r_rcv_buf_addr_ + this->r_rcv_tail_*sizeof(buf), (void *)buf.addr);
  auto sendStatus = this->ep_->PostWriteInline(region.context, &buf, sizeof(buf), this->r_rcv_buf_addr_+this->r_rcv_tail_*sizeof(buf), this->r_rcv_buf_key_);
  if (!sendStatus.ok()){
    return sendStatus;
  }
  // FIXME(fischi) Don't block for that long
  auto wcStatus = this->ep_->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }
  
  debug(stderr, "Appending free buffer [index %d]\n", this->rcv_tail_);
  this->rcv_buffers_[this->rcv_tail_] = buf;
  this->rcv_tail_ = (this->rcv_tail_ + 1) % this->nr_buffers_;
  this->r_rcv_tail_ = (this->r_rcv_tail_ + 1) % this->nr_buffers_;

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
    int ret = ibv_dereg_mr(mr);
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

  // Allocate target buffer
  void *tar_buf = calloc(inflight, sizeof(DirectWriteReceiveBuffer));
  struct ibv_mr *tar_buf_mr = ibv_reg_mr(pd, tar_buf, inflight*sizeof(DirectWriteReceiveBuffer), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (tar_buf_mr == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering target buffer mr");
  }
  

  // Connect to remote
  conn_details local_conn_details;
  local_conn_details.buf_addr = (uint64_t)tar_buf;
  local_conn_details.buf_key = tar_buf_mr->lkey;

  opts.private_data = &local_conn_details;
  opts.private_data_len = sizeof(local_conn_details);
  auto stat = ep->Connect(opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  // Allocate local receiveBuffer
  std::vector<struct ibv_mr *> rcv_buf_mrs;
  for (int i=0; i<inflight; i++){
    void *buf = calloc(1, buf_size);
    struct ibv_mr *tar_buf_mr = ibv_reg_mr(pd, buf, buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (tar_buf_mr == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering reveive buffer mr");
    }
    rcv_buf_mrs.push_back(tar_buf_mr);
  }

  auto allocator = new memory::DumbAllocator(ep->GetPd());

  auto conn = new DirectWriteConnection(ep, allocator,
      inflight, buf_size,
      rcv_buf_mrs,
      tar_buf_mr,
      remote_conn_details->buf_addr, remote_conn_details->buf_key);

  std::chrono::milliseconds timespan(400); // FIXME uggy hack as synchronization is not quite right
  std::this_thread::sleep_for(timespan);

  for (auto mr : rcv_buf_mrs){
    ReceiveRegion reg = {0};
    reg.addr = mr->addr;
    reg.lkey = mr->lkey;

    auto stat = conn->Free(reg);
    if (!stat.ok()){
      conn->Close();
      delete conn;
      return stat;
    }
  }
  std::this_thread::sleep_for(timespan);
  return conn;
}

StatusOr<DirectWriteConnection *> DirectWriteListener::Accept(int32_t buf_size){
  struct ibv_pd *pd = this->listener_->GetPd();
  auto opts = default_opts;

  // Allocate target buffer
  void *tar_buf = calloc(inflight, sizeof(DirectWriteReceiveBuffer));
  struct ibv_mr *tar_buf_mr = ibv_reg_mr(pd, tar_buf, inflight*sizeof(DirectWriteReceiveBuffer), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
  if (tar_buf_mr == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering target buffer mr");
  }
  

  // Connect to remote
  conn_details local_conn_details;
  local_conn_details.buf_addr = (uint64_t)tar_buf;
  local_conn_details.buf_key = tar_buf_mr->lkey;

  opts.private_data = &local_conn_details;
  opts.private_data_len = sizeof(local_conn_details);
  auto ep_stat = this->listener_->Accept(opts);
  if (!ep_stat.ok()){
    return ep_stat.status().Wrap("error connecting to remote");
  }
  auto ep = ep_stat.value();

  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  // Allocate local receiveBuffer
  std::vector<struct ibv_mr *> rcv_buf_mrs;
  for (int i=0; i<inflight; i++){
    void *buf = calloc(1, buf_size);
    struct ibv_mr *tar_buf_mr = ibv_reg_mr(pd, buf, buf_size, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);
    if (tar_buf_mr == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " registering reveive buffer mr");
    }
    rcv_buf_mrs.push_back(tar_buf_mr);
  }

  auto allocator = new memory::DumbAllocator(ep->GetPd());

  debug(stderr, "Registering buffer\n");
  auto conn = new DirectWriteConnection(ep, allocator,
      inflight, buf_size,
      rcv_buf_mrs,
      tar_buf_mr,
      remote_conn_details->buf_addr, remote_conn_details->buf_key);

  std::chrono::milliseconds timespan(400); // FIXME uggy hack as synchronization is not quite right
  std::this_thread::sleep_for(timespan);

  for (auto mr : rcv_buf_mrs){
    ReceiveRegion reg = {0};
    reg.addr = mr->addr;
    reg.lkey = mr->lkey;

    auto stat = conn->Free(reg);
    if (!stat.ok()){
      conn->Close();
      delete conn;
      return stat;
    }
  }
  std::this_thread::sleep_for(timespan);
  return conn;
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


