#include "write_atomic.hpp"

#include <bits/stdint-uintn.h>
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
const int write_atomic_outstanding = 30;
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
        .max_inline_data = sizeof(connectionInfo),
      },
      .qp_type = IBV_QPT_RC,
    },
    .responder_resources = write_atomic_outstanding,
    .initiator_depth =  write_atomic_outstanding,
    .retry_count = 5,  
    .rnr_retry_count = 7, 
  };


  Status sendCI(endpoint::Endpoint *ep, connectionInfo ci){
    auto sendStatus = ep->PostInline(54, &ci, sizeof(ci)); 
    if (!sendStatus.ok()){
      return sendStatus;
    }
    auto wcStatus = ep->PollSendCq();
    if (!wcStatus.ok()){
      return wcStatus.status();
    }
    return Status();
  }
  StatusOr<connectionInfo> receiveCI(endpoint::Endpoint *ep){
    // Getting info on ring buffer
    auto pd = ep->GetPd();
    char* buf = (char*)malloc(sizeof(connectionInfo));
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, sizeof(connectionInfo), IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
    auto rcv_stat = ep->PostRecv(54, mr->lkey, mr->addr, mr->length);
    if (!rcv_stat.ok()){
      return rcv_stat;
    }
    auto rcv_wc_stat = ep->PollRecvCq();
    if (!rcv_wc_stat.ok()){
      return rcv_wc_stat.status();
    }
    struct ibv_wc rcv_wc = rcv_wc_stat.value();
    if (rcv_wc.wr_id != 54) {
      // Bad race
      return Status(StatusCode::Internal, "protocol missmatch");
    }
    connectionInfo ci = *(connectionInfo *)mr->addr;
    free(mr->addr);
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      return Status(StatusCode::Unknown, "error dereg rcv mr");
    }
    return ci;
  }
}


StatusOr<std::unique_ptr<WriteAtomicConnection>> DialWriteAtomic(std::string ip, int port){
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

  auto magic_stat = ringbuffer::GetMagicBuffer(write_atomic_buf_size);
  if (!magic_stat.ok()){
    return magic_stat.status().Wrap("error initialzing magic buffer while dialing");
  }
  auto pd = ep->GetPd();
  struct ibv_mr *buf_mr = ibv_reg_mr(&pd, magic_stat.value(), 2 * write_atomic_buf_size, 
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  struct write_atomic_meta *buf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *buf_meta_mr = ibv_reg_mr(&pd, buf_meta, sizeof(struct write_atomic_meta),
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);  
  struct write_atomic_meta *rbuf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *rbuf_meta_mr = ibv_reg_mr(&pd, rbuf_meta, sizeof(struct write_atomic_meta), IBV_ACCESS_LOCAL_WRITE);  


  auto ci_stat = receiveCI(ep.get());
  if (!ci_stat.ok()){
    return ci_stat.status().Wrap("error receiving connection info while dialing"); 
  }
  connectionInfo ci = ci_stat.value();

  connectionInfo local_ci;
  local_ci.buf_addr = (uint64_t)buf_mr->addr;
  local_ci.buf_key = buf_mr->lkey;
  local_ci.meta_addr = (uint64_t)buf_meta;
  local_ci.meta_key = buf_meta_mr->lkey;
  auto stat = sendCI(ep.get(), local_ci);
  if (!stat.ok()){
    return stat.Wrap("error sending connection info while dialing"); 
  }

  // TODO(fischi) Best case we somehow attach the srq to the ep. I'm not quite sure how to handle that otherwise.
  std::shared_ptr<endpoint::SharedReceiveQueue> srq;

  auto conn = std::make_unique<WriteAtomicConnection>(std::move(ep), buf_mr, buf_meta_mr, srq, alloc,
      rbuf_meta_mr, ci.buf_addr, ci.buf_key, ci.meta_addr, ci.meta_key);

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
  return std::make_unique<WriteAtomicListener>(std::move(ln));
}


WriteAtomicListener::WriteAtomicListener(std::unique_ptr<endpoint::Listener> listener): listener_(std::move(listener)){}

Status WriteAtomicListener::Close(){
  return this->listener_->Close();
}
StatusOr<std::unique_ptr<WriteAtomicConnection>> WriteAtomicListener::Accept(){
  auto epStatus = this->listener_->Accept(default_write_atomic_options);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  std::unique_ptr<endpoint::Endpoint> ep = epStatus.value();
    
    auto alloc = std::make_shared<memory::DumbAllocator>(ep->GetPd());

  auto magic_stat = ringbuffer::GetMagicBuffer(write_atomic_buf_size);
  if (!magic_stat.ok()){
    return magic_stat.status().Wrap("error initialzing magic buffer while accepting");
  }
  auto pd = ep->GetPd();
  struct ibv_mr *buf_mr = ibv_reg_mr(&pd, magic_stat.value(), 2 * write_atomic_buf_size, 
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  struct write_atomic_meta *buf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *buf_meta_mr = ibv_reg_mr(&pd, buf_meta, sizeof(struct write_atomic_meta),
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);  
  struct write_atomic_meta *rbuf_meta = (struct write_atomic_meta *)calloc(1, sizeof(struct write_atomic_meta));
  struct ibv_mr *rbuf_meta_mr = ibv_reg_mr(&pd, rbuf_meta, sizeof(struct write_atomic_meta), IBV_ACCESS_LOCAL_WRITE);  

  connectionInfo local_ci;
  local_ci.buf_addr = (uint64_t)buf_mr->addr;
  local_ci.buf_key = buf_mr->lkey;
  local_ci.meta_addr = (uint64_t)buf_meta;
  local_ci.meta_key = buf_meta_mr->lkey;
  
  auto stat = sendCI(ep.get(), local_ci);
  if (!stat.ok()){
    return stat.Wrap("error sending connection info while accepting"); 
  }
  auto ci_stat = receiveCI(ep.get());
  if (!ci_stat.ok()){
    return ci_stat.status().Wrap("error receiving connection info while accepting"); 
  }
  connectionInfo ci = ci_stat.value();

  // TODO(fischi) Best case we somehow attach the srq to the ep. I'm not quite sure how to handle that otherwise.
  std::shared_ptr<endpoint::SharedReceiveQueue> srq;

   
  auto conn = std::make_unique<WriteAtomicConnection>(std::move(ep), buf_mr, buf_meta_mr, srq, alloc,
      rbuf_meta_mr, ci.buf_addr, ci.buf_key, ci.meta_addr, ci.meta_key);
  return std::move(conn);
}


WriteAtomicConnection::WriteAtomicConnection(std::unique_ptr<endpoint::Endpoint> ep, struct ibv_mr *buf_mr, struct ibv_mr *buf_meta_mr, 
    std::shared_ptr<kym::endpoint::SharedReceiveQueue> srq, std::shared_ptr<memory::Allocator> allocator, struct ibv_mr *rbuf_meta_mr,
    uint64_t rbuf_vaddr, uint32_t rbuf_rkey,  uint64_t rbuf_meta_vaddr, uint32_t rbuf_meta_rkey){
  this->ep_ = std::move(ep);
  this->srq_ = srq;
  this->allocator_ = allocator;

  this->buf_mr_ = buf_mr;
  this->buf_meta_mr_ = buf_meta_mr;
  this->buf_meta_ = (struct write_atomic_meta *)buf_meta_mr->addr;
  this->buf_size_ = write_atomic_buf_size;

  this->rbuf_vaddr_ = rbuf_vaddr;
  this->rbuf_rkey_ = rbuf_rkey;

  this->rbuf_meta_vaddr_ = rbuf_meta_vaddr;
  this->rbuf_meta_rkey_ = rbuf_meta_rkey;
  this->rbuf_meta_mr_ = rbuf_meta_mr;
  this->rbuf_meta_ = (struct write_atomic_meta *)rbuf_meta_mr->addr;

  this->rbuf_size_ = write_atomic_buf_size;
}

Status WriteAtomicConnection::Close(){
  int ret = ibv_dereg_mr(this->buf_meta_mr_);
  if (ret){
    return Status(StatusCode::Internal, "error " + std::to_string(ret) + " deregistering head mr");
  }
  free(buf_meta_mr_->addr);
  ret = ibv_dereg_mr(this->rbuf_meta_mr_);
  if (ret){
    return Status(StatusCode::Internal, "error " + std::to_string(ret) + " deregistering remote head mr");
  }
  free(rbuf_meta_mr_->addr);
  ret = ibv_dereg_mr(this->buf_mr_);
  if (ret){
    return Status(StatusCode::Internal, "error " + std::to_string(ret) + " deregistering ring buffer mr");
  }
  auto stat = ringbuffer::FreeMagicBuffer(this->buf_mr_->addr, write_atomic_buf_size);
  if (!stat.ok()){
    return stat;
  }
  stat = this->ep_->Close();
  if (!stat.ok()){
    return stat;
  }
  return this->ep_->Close();
}
StatusOr<ReceiveRegion> WriteAtomicConnection::Receive(){
  return Status(StatusCode::NotImplemented);
}
Status WriteAtomicConnection::Free(ReceiveRegion reg){
  return Status(StatusCode::NotImplemented);
}
StatusOr<SendRegion> WriteAtomicConnection::GetMemoryRegion(size_t size){
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

Status WriteAtomicConnection::Send(SendRegion region){
  return Status(StatusCode::NotImplemented);
}
Status WriteAtomicConnection::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}


}
}
