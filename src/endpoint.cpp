
/*
 *
 *
 */

#include "endpoint.hpp"

#include <bits/stdint-uintn.h>
#include <cstdio>
#include <iostream>
#include <memory> // For smart pointers

#include <ostream>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h> 
#include <string>

#include "error.hpp"

namespace kym {
namespace endpoint {


Endpoint::Endpoint(rdma_cm_id *id) : id_(id){
}

Endpoint::~Endpoint() {
  rdma_destroy_ep(this->id_);
}

Status Endpoint::Close() {
  int ret = rdma_disconnect(this->id_);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error disconnecting endpoint");
  }
  return Status();
}

ibv_pd Endpoint::GetPd(){
  return *this->id_->pd;
}

Status Endpoint::PostSendRaw(struct ibv_send_wr *wr , struct ibv_send_wr **bad_wr){
  int ret = ibv_post_send(this->id_->qp, wr, bad_wr);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error  " + std::to_string(ret) + " sending");
  }
  return Status();
}

Status Endpoint::PostSend(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey =  lkey;

  struct ibv_send_wr wr, *bad;
  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND;

  wr.send_flags = IBV_SEND_SIGNALED;  
  return this->PostSendRaw( &wr, &bad);
}
Status Endpoint::PostInline(uint64_t ctx, void *addr, size_t size){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;

  struct ibv_send_wr wr, *bad;
  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND;

  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  
  return this->PostSendRaw(&wr, &bad);
}

Status Endpoint::PostImmidate(uint64_t ctx, uint32_t imm){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)&imm;
  sge.length = 0;

  struct ibv_send_wr wr, *bad;
  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.imm_data = imm;
  wr.opcode = IBV_WR_SEND_WITH_IMM;

  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  
  return this->PostSendRaw(&wr, &bad);
}
Status Endpoint::PostRead(uint64_t ctx, uint32_t lkey, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey =  lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = 0;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_READ;
  wr.send_flags = IBV_SEND_SIGNALED;  
  wr.wr.rdma.remote_addr = remote_addr;
  wr.wr.rdma.rkey = rkey;

  return this->PostSendRaw(&wr, &bad);
}
Status Endpoint::PostWrite(uint64_t ctx, uint32_t lkey, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey =  lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.send_flags = IBV_SEND_SIGNALED;  
  wr.wr.rdma.remote_addr = remote_addr;
  wr.wr.rdma.rkey = rkey;

  return this->PostSendRaw(&wr, &bad);
}

Status Endpoint::PostWriteWithImmidate(uint64_t ctx, uint32_t lkey, void *addr, size_t size, 
    uint64_t remote_addr, uint32_t rkey, uint32_t imm){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey =  lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_WRITE_WITH_IMM;
  wr.send_flags = IBV_SEND_SIGNALED;  
  wr.wr.rdma.remote_addr = remote_addr;
  wr.wr.rdma.rkey = rkey;
  wr.imm_data = imm;

  return this->PostSendRaw(&wr, &bad);
}
StatusOr<struct ibv_wc> Endpoint::PollSendCq(){
  struct ibv_wc wc;
  while(ibv_poll_cq(this->id_->qp->send_cq, 1, &wc) == 0){}
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error polling send cq \n" + std::string(ibv_wc_status_str(wc.status)));
  }
  return wc;
}

Status Endpoint::PostRecvRaw(struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr){
  return Status(kym::StatusCode::NotImplemented);
}

Status Endpoint::PostRecv(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey = lkey;

  struct ibv_recv_wr wr, *bad;
  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  int ret = ibv_post_recv(this->id_->qp, &wr, &bad);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error posting receive buffer");
  }
  return Status();
}

StatusOr<ibv_wc> Endpoint::PollRecvCq(){
  struct ibv_wc wc;
  while(ibv_poll_cq(this->id_->qp->recv_cq, 1, &wc) == 0){}
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error polling recv cq\n" + std::string(ibv_wc_status_str(wc.status)));
  }
  return wc;
}

StatusOr<ibv_wc> Endpoint::PollRecvCqOnce(){
  struct ibv_wc wc;
  int ret = ibv_poll_cq(this->id_->qp->recv_cq, 1, &wc);
  if (!ret){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "nothing recieved in send cq");
  }
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error polling recv cq\n" + std::string(ibv_wc_status_str(wc.status)));
  }
  return wc;
}


StatusOr<std::unique_ptr<Endpoint>> Listener::Accept(Options opts){
  int ret;
  struct rdma_cm_id *conn_id;

  ret = rdma_get_request(this->id_, &conn_id);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "accept: error getting request");
  }

  struct rdma_conn_param conn_param;
  conn_param.responder_resources = opts.responder_resources;  
  conn_param.initiator_depth =  opts.initiator_depth; 
  conn_param.retry_count = opts.retry_count;  
  conn_param.rnr_retry_count = opts.rnr_retry_count; 

  conn_param.private_data = opts.private_data;
  conn_param.private_data_len = opts.private_data_len;
  conn_param.flow_control = opts.flow_control;

  ret = rdma_create_qp(conn_id, this->id_->pd, &opts.qp_attr);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "accept: error getting creating qp");
  }

  ret = rdma_accept(conn_id, &conn_param);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "accept: error accepting connection");
  }

  return StatusOr<std::unique_ptr<Endpoint>>(std::make_unique<Endpoint>(conn_id));
}

Listener::Listener(rdma_cm_id *id) : id_(id) {
}

Listener::~Listener(){
}

ibv_pd Listener::GetPd(){
  return *this->id_->pd;
}

Status Listener::Close(){
  int ret = rdma_destroy_id(this->id_);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "error destroying listener");
  }
  return Status();
}

StatusOr<std::unique_ptr<Endpoint>> Dial(std::string ip, int port, Options opts){
  if (ip.empty()){
    return Status(StatusCode::InvalidArgument, "IP cannot be empty");
  }
  int ret; 

  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;

  ret = rdma_getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "Error getting address info");
  }

  struct rdma_cm_id *id;

  // setup endpoint, also creates qp
  ret = rdma_create_ep(&id, addrinfo, NULL, &opts.qp_attr); 
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "Error creating endpoint");
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);


  struct rdma_conn_param conn_param;
  conn_param.responder_resources = opts.responder_resources;  
  conn_param.initiator_depth =  opts.initiator_depth; 
  conn_param.retry_count = opts.retry_count;  
  conn_param.rnr_retry_count = opts.rnr_retry_count; 

  conn_param.private_data = opts.private_data;
  conn_param.private_data_len = opts.private_data_len;
  conn_param.flow_control = opts.flow_control;


  // connect to remote
  ret = rdma_connect(id, &conn_param);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "Error connecting to remote");
  }
  
  return StatusOr<std::unique_ptr<Endpoint>>(std::make_unique<Endpoint>(id));
}

StatusOr<std::unique_ptr<Listener>> Listen(std::string ip, int port){
  int ret;
  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;
  hints.ai_flags = RAI_PASSIVE;

  ret = rdma_getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "Error getting address info");
  }
  
  struct rdma_cm_id *id;
  struct rdma_conn_param conn_param;
 
  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 0;  
  conn_param.initiator_depth =  0;  
  conn_param.retry_count = 8;  
  conn_param.rnr_retry_count = 3; 

  // setup endpoint, also creates qp(?)
  ret = rdma_create_ep(&id, addrinfo, NULL, NULL); 
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "Error creating listening endpoint");
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  ret = rdma_listen(id, 2);
  if(ret){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "listening failed");
  }

  return StatusOr<std::unique_ptr<Listener>>(std::make_unique<Listener>(id));
}

}
}