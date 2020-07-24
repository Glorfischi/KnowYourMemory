
/*
 *
 *  receive_queue.cpp
 *
 *  
 *
 */

# include "receive_queue.hpp"

#include <bits/stdint-uintn.h>
#include <memory>
#include <stddef.h>

#include <infiniband/verbs.h> 
#include <string>
#include <vector>

#include "endpoint.hpp"
#include "error.hpp"

namespace kym {
namespace endpoint {


Status ReceiveQueue::Close(){
  for (auto mr:this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret != 0) {
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " deregistering mr");
    }
    free(mr->addr);
  }
  return Status();
}

struct ibv_mr *ReceiveQueue::GetMR(uint32_t wr_id){
  return this->mrs_[wr_id];
}
Status ReceiveQueue::PostMR(uint32_t wr_id){
  struct ibv_mr *mr = this->mrs_[wr_id];
  return this->ep_->PostRecv(wr_id, mr->lkey, mr->addr, mr->length); 

}
StatusOr<std::unique_ptr<ReceiveQueue>> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight){
  ibv_pd pd = ep->GetPd();
  std::vector<struct ibv_mr*> mrs;
  for (size_t i = 0; i < inflight; i++){

    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    auto regStatus = ep->PostRecv(i, mr->lkey, mr->addr, transfer_size); 
    if (!regStatus.ok()){
      // Best effort, if we fail we can't really do anything about it
      ibv_dereg_mr(mr);
      free(buf);
      for (auto m:mrs){
        ibv_dereg_mr(m);
        free(m->addr);
      }
      return regStatus.Wrap("error setting up receive queue");
    }
    mrs.push_back(mr);

  }
  return std::make_unique<ReceiveQueue>(ep, mrs);
}




Status SharedReceiveQueue::Close(){
  for (auto mr:this->mrs_){
    int ret = ibv_dereg_mr(mr);
    if (ret != 0) {
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " deregistering mr");
    }
    free(mr->addr);
  }
  int ret = ibv_destroy_srq(this->srq_);
  if (ret != 0) {
    return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " destroying srq");
  }
  return Status();
}

struct ibv_mr *SharedReceiveQueue::GetMR(uint32_t wr_id){
  return this->mrs_[wr_id];
}
Status SharedReceiveQueue::PostMR(uint32_t wr_id){
  struct ibv_mr *mr = this->mrs_[wr_id];

  struct ibv_sge sge;
  sge.addr = (uintptr_t)mr->addr;
  sge.length = mr->length;
  sge.lkey = mr->lkey;

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
struct ibv_srq *SharedReceiveQueue::GetSRQ(){
  return this->srq_;
}
StatusOr<std::shared_ptr<SharedReceiveQueue>> GetSharedReceiveQueue(struct ibv_pd pd, size_t transfer_size, size_t inflight){
  struct ibv_srq_init_attr srq_init_attr;
  srq_init_attr.attr.max_sge = 1;
  srq_init_attr.attr.max_wr = inflight;
  auto srq = ibv_create_srq(&pd, &srq_init_attr);
  if (srq == nullptr){
    return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
  }

  std::vector<struct ibv_mr*> mrs;
  for (size_t i = 0; i < inflight; i++){

    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(&pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    struct ibv_sge sge;
    sge.addr = (uintptr_t)mr->addr;
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
      for (auto m:mrs){
        ibv_dereg_mr(m);
        free(m->addr);
      }
      return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " registering buffer for SharedReceiveQueue");
    }
    mrs.push_back(mr);
  }
  return std::make_shared<SharedReceiveQueue>(srq, mrs);

}



}
}
