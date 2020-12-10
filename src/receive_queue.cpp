
/*
 *
 *  receive_queue.cpp
 *
 *  
 *
 */

# include "receive_queue.hpp"

#include <cstdint>
#include <cstdlib>
#include <memory>
#include <ostream>
#include <stddef.h>

#include <infiniband/verbs.h> 
#include <string>
#include <vector>

#include <mutex>

#include "debug.h"
#include "endpoint.hpp"
#include "error.hpp"

namespace kym {
namespace endpoint {


Status ReceiveQueue::Close(){
  int ret = ibv_dereg_mr(this->mr_);
  if (ret) {
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " deregistering mr");
  }
  free(this->mr_->addr);
  return Status();
}

struct mr ReceiveQueue::GetMR(uint32_t wr_id){
  uint64_t addr = ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
  return mr{(void *)addr, this->transfer_size_};
}
Status ReceiveQueue::PostMR(uint32_t wr_id){
  uint64_t addr = ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
  /*int i = --this->current_rcv_mr_;
  this->recv_mr_sge_[i].addr = addr;
  this->recv_mr_wrs_[i].wr_id = wr_id;
  if (this->current_rcv_mr_ == 0) {
    this->current_rcv_mr_ = this->max_rcv_mr_;
    struct ibv_recv_wr *bad;
    auto stat = this->ep_->PostRecvRaw(this->recv_mr_wrs_, &bad);
    return stat;
  }*/
  return this->ep_->PostRecv(wr_id, this->mr_->lkey, (void *)addr, this->transfer_size_);
  return Status();
}

StatusOr<ReceiveQueue *> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight){
  char* buf = (char*)calloc(inflight, transfer_size);
  struct ibv_mr * mr = ibv_reg_mr(ep->GetPd(), buf, transfer_size*inflight, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  for (size_t i = 0; i < inflight; i++){
    debug(stderr, "posting buffer to qp %d, with id %d\n",ep->GetQpNum(), i);
    auto regStatus = ep->PostRecv(i, mr->lkey, (void *)((uint64_t)mr->addr + i*transfer_size), transfer_size); 
    if (!regStatus.ok()){
      // Best effort, if we fail we can't really do anything about it
      ibv_dereg_mr(mr);
      free(buf);
      return regStatus.Wrap("error setting up receive queue");
    }
  }
  int batch_size = 64;
  struct ibv_recv_wr *wrs = (struct ibv_recv_wr *)calloc(batch_size, sizeof(struct ibv_recv_wr));
  struct ibv_sge *sges = (struct ibv_sge *)calloc(batch_size, sizeof(struct ibv_sge));
  for (int i = 0; i < batch_size; i++){
    wrs[i].wr_id = i;
    wrs[i].next = i==batch_size-1 ? NULL : &wrs[i+1];
    wrs[i].sg_list = &sges[i];
    wrs[i].num_sge = 1;
    sges[i].addr = 0;
    sges[i].length = transfer_size;
    sges[i].lkey = mr->lkey;
  }

  return new ReceiveQueue(ep, mr, transfer_size, batch_size, wrs, sges);
}
}
}
