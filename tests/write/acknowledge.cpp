
#include "acknowledge.hpp"
#include "error.hpp"
#include "receive_queue.hpp"
#include <bits/stdint-uintn.h>

namespace kym {
namespace connection {

StatusOr<SendAcknowledger *> GetSendAcknowledger(endpoint::Endpoint *ep){
  return new SendAcknowledger(ep);
}

Status SendAcknowledger::Close(){
  return Status();
}

void SendAcknowledger::Ack(uint32_t offset){
  this->curr_offset_ = offset;
};

Status SendAcknowledger::Flush(){
  auto stat = this->ep_->PostImmidate(0, this->curr_offset_);
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
  auto rq_s = endpoint::GetReceiveQueue(ep, 4, 10);
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
  return offset;
}

}
}
