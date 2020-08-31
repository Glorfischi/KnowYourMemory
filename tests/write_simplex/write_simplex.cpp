#include "write_simplex.hpp"

#include <bits/stdint-uintn.h>
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

#include "conn.hpp"
#include "mm.hpp"
#include "mm/dumb_allocator.hpp"

namespace kym {
namespace connection {

// Max number of outstanding sends
const int write_simplex_outstanding = 10;
const size_t write_simplex_buf_size = 10*1024*1024;

namespace {
struct connectionInfo {
  uint64_t addr;
  uint32_t key;
};

endpoint::Options default_snd_options = {
  .qp_attr = {
    .cap = {
      .max_send_wr = write_simplex_outstanding,
      .max_recv_wr = write_simplex_outstanding,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = sizeof(connectionInfo),
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 0,
  .initiator_depth =  write_simplex_outstanding,
  .retry_count = 8,  
  .rnr_retry_count = 7, 
};
endpoint::Options default_rcv_options = {
  .qp_attr = {
    .cap = {
      .max_send_wr = write_simplex_outstanding,
      .max_recv_wr = write_simplex_outstanding,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 2*sizeof(connectionInfo)
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 2*write_simplex_outstanding,
  .initiator_depth = 0,
  .retry_count = 8,  
  .rnr_retry_count = 7, 
};
}


StatusOr<WriteSimplexSender *> DialWriteSimplex(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto opts = default_snd_options;
  auto ep_stat = kym::endpoint::Dial(ip, port, opts);
  if (!ep_stat.ok()){
    return ep_stat.status();
  }

  endpoint::Endpoint *ep = ep_stat.value();

  // Getting info on ring buffer
  auto pd = ep->GetPd();
  char* buf = (char*)malloc(sizeof(connectionInfo));
  struct ibv_mr * mr = ibv_reg_mr(&pd, buf, sizeof(connectionInfo), IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  auto rcv_stat = ep->PostRecv(53, mr->lkey, mr->addr, mr->length);
  if (!rcv_stat.ok()){
    return rcv_stat;
  }

  auto rcv_wc_stat = ep->PollRecvCq();
  if (!rcv_wc_stat.ok()){
    return rcv_wc_stat.status();
  }

  struct ibv_wc rcv_wc = rcv_wc_stat.value();
  if (rcv_wc.wr_id != 53) {
    // Bad race
    return Status(StatusCode::Unknown, "protocol missmatch");
  }
  connectionInfo ci = *(connectionInfo *)mr->addr;

  free(mr->addr);
  int ret = ibv_dereg_mr(mr);
  if (ret) {
    return Status(StatusCode::Unknown, "error dereg rcv mr");
  }

  std::vector<struct ibv_mr *> ack_mrs;
  size_t transfer_size = sizeof(uint32_t);
  for (int i = 0; i < write_simplex_outstanding; i++){
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    auto regStatus = ep->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    ack_mrs.push_back(mr);
  }
  auto alloc = new memory::DumbAllocator(ep->GetPd());

  auto sndr = new WriteSimplexSender(ep, alloc, ack_mrs, ci.addr, ci.key);
  return sndr;
}

WriteSimplexSender::WriteSimplexSender(endpoint::Endpoint *ep, memory::Allocator *allocator,
    std::vector<struct ibv_mr *> ack_mrs, uint64_t buf_vaddr, uint32_t buf_rkey){
  this->ep_ = ep;
  this->allocator_ = allocator;
  this->ack_mrs_ = ack_mrs;
  this->buf_vaddr_ = buf_vaddr;
  this->buf_rkey_ = buf_rkey;

  this->buf_full_ = false;
  this->buf_head_ = 0;
  this->buf_tail_ = 0;
  this->buf_size_ = write_simplex_buf_size;

  this->ack_outstanding_ = 0;
}

WriteSimplexSender::~WriteSimplexSender(){
  delete this->ep_;
  delete this->allocator_;
}
Status WriteSimplexSender::Close(){
  for (auto mr : this->ack_mrs_){
    free(mr->addr);
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      return Status(StatusCode::Unknown, "error dereg ack mr");
    }
  }
  return this->ep_->Close();
}

StatusOr<SendRegion> WriteSimplexSender::GetMemoryRegion(size_t size){
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
Status WriteSimplexSender::Send(SendRegion region){
  // We poll the recieve queue. If we received an ack we can move the head and free space.
  auto wcStatus = this->ep_->PollRecvCqOnce();
  while (wcStatus.ok()){
    // We will try again until the receive queue is empty. If there are already as many acks outstanding as 
    struct ibv_wc wc = wcStatus.value();
    struct ibv_mr *mr = this->ack_mrs_[wc.wr_id];
    this->buf_head_ = *(uint32_t *)mr->addr;
    this->buf_full_ = false;
    auto repost_s = ep_->PostRecv(wc.wr_id, mr->lkey, mr->addr, mr->length); 
    if (!repost_s.ok()){
      return repost_s;
    }
    wcStatus = this->ep_->PollRecvCqOnce();
  }

  // Check if there is space to send
  // TODO(Fischi) Should we return or block?
  if (this->buf_head_ == this->buf_tail_ && this->buf_full_){
    return Status(StatusCode::Unknown, "buffer is full");
  } else if (this->buf_head_ == this->buf_tail_ && region.length > this->buf_size_){
    // We are empty, but the message is larger then our complete buffer.
    return Status(StatusCode::Unknown, "message larger then buffer");
  } else if (this->buf_head_ < this->buf_tail_ 
      && this->buf_tail_ + region.length > this->buf_size_
      && region.length > this->buf_head_){
    // The free region does "wrap around" the end of the buffer. It either needs to fit in front of the used space or
    // behind it.
    return Status(StatusCode::Unknown, "not enough free space for message");
  } else if (this->buf_head_ > this->buf_tail_ 
      && this->buf_head_ - this->buf_tail_ < region.length){
    // The free region is continuous. The message needs to be at most as big. 
    return Status(StatusCode::Unknown, "not enough free space for message");
  }

  // We either write directly at the tail if there is enough space or we wrap around to the beginning of the buffer.
  uint32_t write_offset = (this->buf_tail_ + region.length <= this->buf_size_) ? this->buf_tail_ : 0;
  this->buf_tail_ = write_offset + region.length; 
  if (this->buf_tail_ == this->buf_head_){
    // We filled the buffer completely.
    this->buf_full_ = true;
  }
  auto send_stat = this->ep_->PostWriteWithImmidate(1, region.lkey, region.addr, region.length, 
      this->buf_vaddr_ + write_offset, this->buf_rkey_, region.length);
  if (!send_stat.ok()){
    return send_stat;
  }
  auto wc_stat = this->ep_->PollSendCq();
  if (!wc_stat.ok()){
    return wc_stat.status();
  }
  return Status();
}

Status WriteSimplexSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->allocator_->Free(mr);
}



StatusOr<WriteSimplexListener *> ListenWriteSimplex(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  
  auto ln_stat = endpoint::Listen(ip, port);
  if (!ln_stat.ok()){
    return ln_stat.status();
  }
  endpoint::Listener *ln = ln_stat.value();
  return new WriteSimplexListener(ln);
}

StatusOr<WriteSimplexReceiver *> WriteSimplexListener::Accept(){
  auto opts = default_rcv_options;
  auto epStatus = this->listener_->Accept(opts);
  if (!epStatus.ok()){
    return epStatus.status();
  }
  endpoint::Endpoint *ep = epStatus.value();

  auto pd = ep->GetPd();
  size_t buf_size = write_simplex_buf_size;
  char* buf = (char*)malloc(buf_size);
  struct ibv_mr *buf_mr = ibv_reg_mr(&pd, buf, buf_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

  std::vector<struct ibv_mr *> rcv_mrs;
  size_t transfer_size = sizeof(uint32_t);
  for (int i = 0; i < write_simplex_outstanding; i++){
    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    auto regStatus = ep->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    rcv_mrs.push_back(mr);
  }


  // Sending info on buffer memory region
  connectionInfo ci;
  ci.key = buf_mr->lkey;
  ci.addr = (uint64_t)buf_mr->addr;
  auto sendStatus = ep->PostInline(53, &ci, sizeof(ci)); 
  if (!sendStatus.ok()){
    return sendStatus;
  }
  auto wcStatus = ep->PollSendCq();
  if (!wcStatus.ok()){
    return wcStatus.status();
  }


  auto rcvr = new WriteSimplexReceiver(ep, buf_mr, rcv_mrs);
  return rcvr;
}

WriteSimplexListener::WriteSimplexListener(endpoint::Listener *listener) : listener_(listener) {}
WriteSimplexListener::~WriteSimplexListener(){
  delete this->listener_;
}

Status WriteSimplexListener::Close() {
  return this->listener_->Close();
}


WriteSimplexReceiver::WriteSimplexReceiver(endpoint::Endpoint *ep, struct ibv_mr *buf_mr, 
    std::vector<struct ibv_mr *> rcv_mrs){
    this->ep_ = ep;
    
    this->rcv_mrs_ = rcv_mrs;
    this->buf_mr_ = buf_mr;
    this->buf_head_ = 0;
    this->buf_ack_head_ = 0;
    this->buf_max_unack_ = buf_mr->length/8;
}

WriteSimplexReceiver::~WriteSimplexReceiver(){
  delete this->ep_;
}

Status WriteSimplexReceiver::Close(){
  free(this->buf_mr_->addr);
  int ret = ibv_dereg_mr(this->buf_mr_);
  if (ret) {
    return Status(StatusCode::Unknown, "error dereg buffer mr");
  }
  for (auto mr : this->rcv_mrs_){
    free(mr->addr);
    int ret = ibv_dereg_mr(mr);
    if (ret) {
      return Status(StatusCode::Unknown, "error dereg rcv mr");
    }
  }
  return this->ep_->Close();
}

StatusOr<ReceiveRegion> WriteSimplexReceiver::Receive(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status().Wrap("error polling receive cq");
  }
  struct ibv_wc wc = wcStatus.value();
  uint32_t msg_len = wc.imm_data;

  struct ibv_mr *mr = this->rcv_mrs_[wc.wr_id];
  auto repost_s = ep_->PostRecv(wc.wr_id, mr->lkey, mr->addr, mr->length); 
  if (!repost_s.ok()){
    return repost_s.Wrap("error reposting buffer");
  }

  // We either read at the head or a the beginning of the buffer if there is not enough space
  uint32_t read_offset = (this->buf_head_ + msg_len <= this->buf_mr_->length) ? this->buf_head_ : 0;
  ReceiveRegion reg;
  reg.addr = (void *)((uint64_t)this->buf_mr_->addr + read_offset);
  reg.length = msg_len;
 
  return reg;
}
Status WriteSimplexReceiver::Free(ReceiveRegion reg){
  // TODO(fischi) How do we handle out of order frees?
  // Get the new head after we freed. Either we read at the old head or at the beginning of the buffer
  this->buf_head_  = (this->buf_head_ + reg.length <= this->buf_mr_->length) ? this->buf_head_ + reg.length : reg.length;

  if (
      (this->buf_ack_head_ < this->buf_head_ 
       && this->buf_ack_head_ + this->buf_max_unack_ <= this->buf_head_ ) ||
      (this->buf_ack_head_ > this->buf_head_ 
       && (this->buf_mr_->length - this->buf_ack_head_) + this->buf_head_ >= this->buf_max_unack_)
      ){
    auto sendStatus = this->ep_->PostInline(0, &this->buf_head_, sizeof(this->buf_head_)); 
    if (!sendStatus.ok()){
      return sendStatus.Wrap("error sending ack");
    }

    auto wcStatus = this->ep_->PollSendCq();
    if (!wcStatus.ok()){
      return wcStatus.status().Wrap("error polling ack send cq");
    }
    this->buf_ack_head_ = this->buf_head_;
  }
  return Status();
}



}
}
