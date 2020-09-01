
/*
 *
 *
 */

#include "endpoint.hpp"

#include <bits/stdint-uintn.h>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory> // For smart pointers

#include <ostream>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h> 
#include <string>

#include "error.hpp"

namespace kym {
namespace endpoint {

int get_rdma_addr(const char *src, const char *dst, const char *port,
		  struct rdma_addrinfo *hints, struct rdma_addrinfo **rai){
	struct rdma_addrinfo rai_hints, *res;
	int ret;

	if (hints->ai_flags & RAI_PASSIVE)
		return rdma_getaddrinfo(src, port, hints, rai);

	rai_hints = *hints;
	if (src) {
		rai_hints.ai_flags |= RAI_PASSIVE;
		ret = rdma_getaddrinfo(src, NULL, &rai_hints, &res);
		if (ret)
			return ret;

		rai_hints.ai_src_addr = res->ai_src_addr;
		rai_hints.ai_src_len = res->ai_src_len;
		rai_hints.ai_flags &= ~RAI_PASSIVE;
	}

	ret = rdma_getaddrinfo(dst, port, &rai_hints, rai);
	if (src)
		rdma_freeaddrinfo(res);

	return ret;
}


Endpoint::Endpoint(rdma_cm_id *id) : id_(id){
  // std::cout << "dev " << id->verbs->device->name << std::endl;
  // std::cout << "id " << id <<  " pd " << id->pd << " qp " << id->qp << " verbs " << id->verbs << std::endl; 
}
Endpoint::Endpoint(rdma_cm_id* id, void *private_data, size_t private_data_len) : id_(id), private_data_(private_data), private_data_len_(private_data_len){
  // std::cout << "dev " << id->verbs->device->name << std::endl;
  // std::cout << "id " << id <<  " pd " << id->pd << " qp " << id->qp << " verbs " << id->verbs << std::endl; 
}

Endpoint::~Endpoint() {
  if (this->private_data_){
    free(this->private_data_);
  }
  rdma_destroy_ep(this->id_);
}

Status Endpoint::Close() {
  int ret = rdma_disconnect(this->id_);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "error disconnecting endpoint");
  }
  return Status();
}

ibv_pd Endpoint::GetPd(){
  return *this->id_->pd;
}
ibv_srq *Endpoint::GetSRQ(){
  return this->id_->srq;
}
struct ibv_cq *Endpoint::GetSendCQ(){
  return this->id_->send_cq;
}
struct ibv_cq	*Endpoint::GetRecvCQ(){
  return this->id_->recv_cq;
}

size_t Endpoint::GetConnectionInfo(void ** buf){
  *buf = this->private_data_;
  return this->private_data_len_;
}

Status Endpoint::PostSendRaw(struct ibv_send_wr *wr , struct ibv_send_wr **bad_wr){
  int ret = ibv_post_send(this->id_->qp, wr, bad_wr);
  if (ret) {
    // TODO(Fischi) Map error codes
    perror("SEND");
    return Status(StatusCode::Internal, "error  " + std::to_string(ret) + " sending");
  }
  return Status();
}

Status Endpoint::PostSend(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  return PostSend(ctx, lkey, addr, size, true);
}
Status Endpoint::PostSend(uint64_t ctx, uint32_t lkey, void *addr, size_t size, bool signaled){
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

  wr.send_flags = signaled ? IBV_SEND_SIGNALED : 0;  
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

Status Endpoint::PostFetchAndAdd(uint64_t ctx, uint64_t add, uint32_t lkey, uint64_t *addr, uint64_t remote_addr, uint32_t rkey){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = sizeof(uint64_t);
  sge.lkey =  lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
  wr.send_flags = IBV_SEND_SIGNALED;  
  wr.wr.atomic.remote_addr = remote_addr;
  wr.wr.atomic.rkey = rkey;
  wr.wr.atomic.compare_add = add;

  return this->PostSendRaw(&wr, &bad);
}

StatusOr<struct ibv_wc> Endpoint::PollSendCq(){
  struct ibv_wc wc;
  while(ibv_poll_cq(this->id_->qp->send_cq, 1, &wc) == 0){}
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "error " + std::to_string(wc.status) +  " polling send cq for wr " 
        + std::to_string(wc.wr_id ) +" \n" + std::string(ibv_wc_status_str(wc.status)));
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

  int ret;
  if(this->id_->srq != nullptr){
    ret = ibv_post_srq_recv(this->id_->srq, &wr, &bad);
  } else {
    ret = ibv_post_recv(this->id_->qp, &wr, &bad);
  }
  if (ret) {
    return Status(StatusCode::Internal, "error posting receive buffer");
  }
  return Status();
}

StatusOr<ibv_wc> Endpoint::PollRecvCq(){
  struct ibv_wc wc;
  while(ibv_poll_cq(this->id_->qp->recv_cq, 1, &wc) == 0){}
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "error " + std::to_string(wc.status) +  " polling recv cq\n" + std::string(ibv_wc_status_str(wc.status)));
  }
  return wc;
}

StatusOr<ibv_wc> Endpoint::PollRecvCqOnce(){
  struct ibv_wc wc;
  int ret = ibv_poll_cq(this->id_->qp->recv_cq, 1, &wc);
  if (!ret){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "nothing recieved in send cq");
  }
  if (wc.status){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "error polling recv cq\n" + std::string(ibv_wc_status_str(wc.status)));
  }
  return wc;
}


StatusOr<Endpoint *> Listener::Accept(Options opts){
  int ret;
  struct rdma_cm_id *conn_id;

  ret = rdma_get_request(this->id_, &conn_id);
  if (ret){
    return Status(StatusCode::Internal, "accept: error getting connection id");
  }
  // Set connection data
  size_t private_data_len = conn_id->event->param.conn.private_data_len;
  void * private_data;
  if (private_data_len) {
    private_data = calloc(1, private_data_len);
    memcpy(private_data, conn_id->event->param.conn.private_data, private_data_len);
  }
  
  struct rdma_conn_param conn_param;
  conn_param.responder_resources = opts.responder_resources;  
  conn_param.initiator_depth =  opts.initiator_depth; 
  conn_param.retry_count = opts.retry_count;  
  conn_param.rnr_retry_count = opts.rnr_retry_count; 

  conn_param.private_data = opts.private_data;
  conn_param.private_data_len = opts.private_data_len;
  conn_param.flow_control = opts.flow_control;

  if (opts.use_srq && opts.qp_attr.srq == nullptr){
    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.attr.max_sge = opts.qp_attr.cap.max_recv_sge;
    srq_init_attr.attr.max_wr = opts.qp_attr.cap.max_recv_wr;
    auto srq = ibv_create_srq(conn_id->pd, &srq_init_attr);
    if (srq == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
    }
    opts.qp_attr.srq = srq;
  }

  ret = rdma_create_qp(conn_id, this->id_->pd, &opts.qp_attr);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "accept: error getting creating qp");
  }
  
  ret = rdma_accept(conn_id, &conn_param);
  if (ret) {
    return Status(StatusCode::Internal, "accept: error accepting connection");
  }
  conn_id->srq = opts.qp_attr.srq;
  
  return new Endpoint(conn_id, private_data, private_data_len);
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
    return Status(StatusCode::Internal, "error destroying listener");
  }
  return Status();
}

StatusOr<Endpoint *> Create(std::string ip, int port, Options opts){
  if (ip.empty()){
    return Status(StatusCode::InvalidArgument, "IP cannot be empty");
  }
  int ret; 

  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;

  ret = get_rdma_addr(opts.src, ip.c_str(), std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    return Status(StatusCode::Internal, "Error getting address info");
  }

  struct rdma_cm_id *id;

  // setup endpoint, also creates qp
  ret = rdma_create_ep(&id, addrinfo, opts.pd, NULL); 
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating endpoint");
  }
  if (opts.use_srq && opts.qp_attr.srq == nullptr){
    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.attr.max_sge = opts.qp_attr.cap.max_recv_sge;
    srq_init_attr.attr.max_wr = opts.qp_attr.cap.max_recv_wr;
    auto srq = ibv_create_srq(id->pd, &srq_init_attr);
    if (srq == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
    }
  }
  ret = rdma_create_qp(id, id->pd, &opts.qp_attr);
  if (ret) {
    return Status(StatusCode::Internal, "dial: error getting creating qp");
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  return new Endpoint(id);
}
Status Endpoint::Connect(Options opts){
  struct rdma_conn_param conn_param;
  conn_param.responder_resources = opts.responder_resources;  
  conn_param.initiator_depth =  opts.initiator_depth; 
  conn_param.retry_count = opts.retry_count;  
  conn_param.rnr_retry_count = opts.rnr_retry_count; 

  conn_param.private_data = opts.private_data;
  conn_param.private_data_len = opts.private_data_len;
  conn_param.flow_control = opts.flow_control;

  // connect to remote
  int ret = rdma_connect(this->id_, &conn_param);
  if (ret) {
    return Status(StatusCode::Internal, "Error connecting to remote");
  }
  // Set connection data
  size_t private_data_len = this->id_->event->param.conn.private_data_len;
  void * private_data = nullptr;
  if (private_data_len) {
    private_data = calloc(1, private_data_len);
    memcpy(private_data, this->id_->event->param.conn.private_data, private_data_len);
  }
  this->private_data_ = private_data;
  this->private_data_len_ = private_data_len;
  return Status();
}

StatusOr<Endpoint *> Dial(std::string ip, int port, Options opts){
  auto ep_stat = Create(ip, port, opts);
  if (!ep_stat.ok()){
    return ep_stat.status().Wrap("error setting up endpoint");
  }
  auto ep = ep_stat.value();
  auto stat = ep->Connect(opts);
  if (!stat.ok()){
    return stat;
  }
  return ep;
}

StatusOr<Listener *> Listen(std::string ip, int port){
  int ret;
  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;
  hints.ai_flags = RAI_PASSIVE;

  ret = get_rdma_addr(ip.c_str(), NULL, std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "Error getting address info");
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
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) +" creating listening endpoint");
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  ret = rdma_listen(id, 2);
  if(ret){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "listening failed");
  }

  return new Listener(id);
}

}
}
