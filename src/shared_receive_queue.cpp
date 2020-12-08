
/*
 *
 *  receive_queue.cpp
 *
 *  
 *
 */

#include "shared_receive_queue.hpp"

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

#include "readerwriterqueue.h"

namespace kym {
namespace endpoint {

struct mr SharedReceiveQueue::GetMR(uint32_t wr_id){
  uint64_t addr = this->ContextToAddr(wr_id);
  return mr{(void *)addr, this->transfer_size_};
}
Status SharedReceiveQueue::PostMR(uint32_t wr_id){
  debug(stderr, "Posting mr %d\n", wr_id);
  uint64_t addr = ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
  int i = --this->current_rcv_mr_;
  this->recv_mr_sge_[i].addr = addr;
  this->recv_mr_wrs_[i].wr_id = wr_id;
  if (this->current_rcv_mr_ == 0) {
    this->current_rcv_mr_ = this->max_rcv_mr_;
    struct ibv_recv_wr *bad;
    int ret = ibv_post_srq_recv(this->srq_, this->recv_mr_wrs_, &bad);
    if (ret) {
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " reposting buffer to SharedReceiveQueue");
    }
  }
  return Status();
}

StatusOr<SharedReceiveQueueStub *> SharedReceiveQueue::NewReceiver(){
  moodycamel::ReaderWriterQueue<uint32_t> *q = new moodycamel::ReaderWriterQueue<uint32_t>(100000);
  this->queues_.push_back(q);
  return new SharedReceiveQueueStub(this, q);
}

Status SharedReceiveQueue::Run() {
  while (true) {
    for (auto q : this->queues_){
      uint32_t wr_id;
      bool suc = q->try_dequeue(wr_id);
      if (suc) {
        auto stat = this->PostMR(wr_id);
        if (!stat.ok()) {
          return stat;
        }
      }
    }
  }
}


Status SharedReceiveQueueStub::PostMR(uint32_t wr_id){
  bool succeded = this->queue_->try_enqueue(wr_id);
  if (!succeded){
    return Status(StatusCode::RateLimit, "inter process queue overrun");
  }
  return Status();
}

struct mr SharedReceiveQueueStub::GetMR(uint32_t wr_id){
  uint64_t addr = this->parent_->ContextToAddr(wr_id);
  return mr{(void *)addr, this->parent_->transfer_size_};
}

Status SharedReceiveQueueStub::Close(){
  return Status();
}


struct ibv_srq *SharedReceiveQueue::GetSRQ(){
  return this->srq_;
}

Status SharedReceiveQueue::Close(){
  int ret = ibv_dereg_mr(this->mr_);
  if (ret) {
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " deregistering mr");
  }
  free(this->mr_->addr);

  ret = ibv_destroy_srq(this->srq_);
  if (ret != 0) {
    return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " destroying srq");
  }
  return Status();
}

StatusOr<SharedReceiveQueue *> GetSharedReceiveQueue(struct ibv_pd *pd, size_t transfer_size, size_t inflight){
  struct ibv_srq_init_attr srq_init_attr = {0};
  srq_init_attr.attr.max_sge = 1;
  srq_init_attr.attr.max_wr = inflight;
  auto srq = ibv_create_srq(pd, &srq_init_attr);
  if (srq == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
  }
  
  debug(stderr, "Created SRQ [ctx: %p, srq_ctx: %p]\n", srq->context, srq->srq_context);

  char* buf = (char*)calloc(inflight, transfer_size);
  struct ibv_mr * mr = ibv_reg_mr(pd, buf, transfer_size*inflight, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

  for (size_t i = 0; i < inflight; i++){
    struct ibv_sge sge;
    sge.addr = ((uint64_t)mr->addr + i*transfer_size);
    sge.length = transfer_size;
    sge.lkey = mr->lkey;

    struct ibv_recv_wr wr, *bad;
    wr.wr_id = i;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    int ret = ibv_post_srq_recv(srq, &wr, &bad);
    if (ret) {
      // Best effort, if we fail we can't really do anything about it
      ibv_dereg_mr(mr);
      free(buf);
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " registering buffer for SharedReceiveQueue");
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

  return new SharedReceiveQueue(srq, mr, transfer_size, batch_size, wrs, sges);

}

}
}
