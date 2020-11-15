
/*
 *
 *  receive_queue.cpp
 *
 *  
 *
 */

# include "receive_queue.hpp"

#include <cstdint>
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
  uint64_t addr = ContextToAddr(wr_id);
  return mr{(void *)addr, this->transfer_size_};
}

Status ReceiveQueue::PostMR(uint32_t wr_id){
  uint64_t addr = ContextToAddr(wr_id);
  return this->ep_->PostRecv(wr_id, this->mr_->lkey, (void *)addr, this->transfer_size_); 
}

int ReceiveQueue::FastPostMR(uint32_t *array, int size){
  for(int i=0; i<size; i++){
    recv_sges[i].addr = ContextToAddr(array[i]);
    recv_wrs[i].wr_id = array[i];
  }
  recv_wrs[size-1].next = NULL; // end of the batch. so *size* can be smaller  than 128
  
  struct ibv_recv_wr* badwr;
  int ret = this->ep_->FastPostRecvRaw(recv_wrs,&badwr); 
  recv_wrs[size-1].next = &recv_wrs[size];
  return ret;
}
 

StatusOr<ReceiveQueue *> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight){
  inflight=inflight*5;
  printf("Create Get Receive with inflight %lu (Note I increased it!)\n",inflight );

  char* buf = (char*)calloc(inflight, transfer_size);
  struct ibv_mr * mr = ibv_reg_mr(ep->GetPd(), buf, transfer_size*inflight, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  
  for (size_t i = 0; i < inflight; i++){
    auto regStatus = ep->PostRecv(i, mr->lkey, (void *)((uint64_t)mr->addr + i*transfer_size), transfer_size); 
    if (!regStatus.ok()){
      // Best effort, if we fail we can't really do anything about it
      ibv_dereg_mr(mr);
      free(buf);
      return regStatus.Wrap("error setting up receive queue");
    }
  }
  return new ReceiveQueue(ep, mr, transfer_size);
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

struct mr SharedReceiveQueue::GetMR(uint32_t wr_id){
  uint64_t addr = ContextToAddr(wr_id);
  return mr{(void *)addr, this->transfer_size_};
}

Status SharedReceiveQueue::PostMR(uint32_t wr_id){
  uint64_t addr = ContextToAddr(wr_id);

  struct ibv_sge sge;
  sge.addr = addr;
  sge.length = this->transfer_size_;
  sge.lkey = this->mr_->lkey;

  struct ibv_recv_wr wr, *bad;
  wr.wr_id = wr_id;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
 
  int ret = ibv_post_srq_recv(this->srq_, &wr, &bad);
  if (ret) {
    return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " reposting buffer to SharedReceiveQueue");
  }
  return Status();
}

int SharedReceiveQueue::FastPostMR(uint32_t *array, int size){
  for(int i=0; i<size; i++){
    recv_sges[i].addr = ContextToAddr(array[i]);
    recv_wrs[i].wr_id = array[i];
  }
  recv_wrs[size-1].next = NULL;

  struct ibv_recv_wr* badwr;
  int ret = ibv_post_srq_recv(this->srq_, recv_wrs, &badwr);
  recv_wrs[size-1].next = &recv_wrs[size];
  return ret;
}

struct ibv_srq *SharedReceiveQueue::GetSRQ(){
  return this->srq_;
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
  printf("Create Get Receive with inflight %lu \n",inflight );
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
  return new SharedReceiveQueue(srq, mr, transfer_size);

}



}
}
