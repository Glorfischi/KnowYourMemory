
#include "acknowledge.hpp"
#include "error.hpp"
#include "receive_queue.hpp"
#include <algorithm>
#include <cstdlib>
#include <infiniband/verbs.h>
#include <string>

#include "debug.h"

namespace {
  int d_ack = 0;
}

namespace kym {
namespace connection {

StatusOr<SendAcknowledger *> GetSendAcknowledger(endpoint::Endpoint *ep){
  return new SendAcknowledger(ep);
}

Status SendAcknowledger::Close(){
  return Status();
}

void SendAcknowledger::Ack(volatile uint32_t offset){
  this->curr_offset_ = offset;
};

Status SendAcknowledger::Flush(){
  auto stat = this->ep_->PostInline(0, &this->curr_offset_, sizeof(uint32_t));
  if (!stat.ok()){
    return stat;
  }
  auto wc_s = this->ep_->PollSendCq();
  if (!wc_s.ok()){
    return wc_s.status();
  }
  return Status();
};

AcknowledgerContext SendAcknowledger::GetContext(){
  return AcknowledgerContext{};
}


StatusOr<SendAckReceiver *> GetSendAckReceiver(endpoint::Endpoint *ep){
  auto rq_s = endpoint::GetReceiveQueue(ep, 4, 100);
  if (!rq_s.ok()){
    return rq_s.status().Wrap("Error setting up receive queue");
  }
  return new SendAckReceiver(ep, rq_s.value());
}

SendAckReceiver::~SendAckReceiver(){
  delete this->rq_;
}
Status SendAckReceiver::Close(){
  return this->rq_->Close();
}

StatusOr<uint32_t> SendAckReceiver::Get(){
  auto wcStatus = this->ep_->PollRecvCq();
  if (!wcStatus.ok()){
    return wcStatus.status().Wrap("error while polling recieve queue for receive");
  }

  struct ibv_wc wc = wcStatus.value();

  auto mr  = this->rq_->GetMR(wc.wr_id);
  uint32_t offset = *(uint32_t *)mr.addr;

  auto stat = this->rq_->PostMR(wc.wr_id);
  if (!stat.ok()){
    return stat;
  }

  while (true) {
    auto wc_s = this->ep_->PollRecvCqOnce();
    if (!wc_s.ok()){
      break;
    }
    struct ibv_wc wc = wcStatus.value();

    auto mr  = this->rq_->GetMR(wc.wr_id);
    offset = *(uint32_t *)mr.addr;

    auto stat = this->rq_->PostMR(wc.wr_id);
    if (!stat.ok()){
      return stat;
    }
  }
  return offset;
}



StatusOr<ReadAcknowledger *> GetReadAcknowledger(struct ibv_pd *pd){
  void *offset = calloc(1, sizeof(uint32_t));
  if (offset == nullptr){
    Status(StatusCode::Internal, "Error allocating memory");
  }
  auto mr = ibv_reg_mr(pd, offset, sizeof(offset), IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);
  if (mr == nullptr){
    Status(StatusCode::Internal, "Error registering mr");
  }
  return new ReadAcknowledger(mr);
}
ReadAcknowledger::ReadAcknowledger(struct ibv_mr *mr) : mr_(mr){
  this->curr_offset_ = (uint32_t *) mr->addr;
};
Status ReadAcknowledger::Close() {
  int ret = ibv_dereg_mr(this->mr_);
  if (ret){
    return Status(StatusCode::Internal, "Error deregistering mr " + std::to_string(ret));
  }
  free(this->mr_->addr);
  return Status();
}
void ReadAcknowledger::Ack(volatile uint32_t off){
  *this->curr_offset_ = off;
}

Status ReadAcknowledger::Flush(){
  return Status();
}

AcknowledgerContext ReadAcknowledger::GetContext(){
  auto ctx = AcknowledgerContext();
  *(uint64_t *)ctx.data = (uint64_t)this->mr_->addr;
  ctx.data[2] = this->mr_->lkey;
  return ctx;
}

StatusOr<ReadAckReceiver *> GetReadAckReceiver(endpoint::Endpoint *ep, AcknowledgerContext ctx){
  void *offset = calloc(1, sizeof(uint32_t));
  if (offset == nullptr){
    Status(StatusCode::Internal, "Error allocating memory");
  }
  auto mr = ibv_reg_mr(ep->GetPd(), offset, sizeof(offset), IBV_ACCESS_LOCAL_WRITE);
  if (mr == nullptr){
    Status(StatusCode::Internal, "Error registering mr");
  }
  return new ReadAckReceiver(ep, mr, ctx.data[2], *(uint64_t *)ctx.data);
}
ReadAckReceiver::ReadAckReceiver(endpoint::Endpoint *ep, struct ibv_mr * mr, uint32_t key, uint64_t addr) 
  : ep_(ep), key_(key), addr_(addr), mr_(mr) {
  this->curr_offset_ = (uint32_t *)mr->addr;
}
Status ReadAckReceiver::Close(){
  int ret = ibv_dereg_mr(this->mr_);
  if (ret){
    return Status(StatusCode::Internal, "Error deregistering mr " + std::to_string(ret));
  }
  free(this->mr_->addr);
  info(stderr, "Got new head %d times\n", d_ack);
  return this->ep_->Close();
}
StatusOr<uint32_t> ReadAckReceiver::Get(){
  d_ack++;
  auto stat = this->ep_->PostRead(0, this->mr_->lkey, this->mr_->addr, sizeof(uint32_t), this->addr_, this->key_);
  if (!stat.ok()){
    return stat;
  }
  // This will block until we actually updated 
  int id = 1;
  while (id != 0){
    auto wc_s = this->ep_->PollSendCq();
    if (!wc_s.ok()){
      return wc_s.status();
    }
    id = wc_s.value().wr_id;
  }
  
  return *this->curr_offset_;
}



}
}
