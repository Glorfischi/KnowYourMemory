
/*
 *
 *
 */

#include "endpoint.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <memory> // For smart pointers

#include <ostream>
#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h> 
#include <infiniband/mlx5dv.h>

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

ibv_context *Endpoint::GetContext(){
  return this->id_->verbs;
}
ibv_pd *Endpoint::GetPd(){
  return this->id_->pd;
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
    perror("SEND Error");
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

Status Endpoint::PostWriteInline(uint64_t ctx, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey){
  struct ibv_sge sge;
  sge.addr = (uintptr_t)addr;
  sge.length = size;
  sge.lkey =  0;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = ctx;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_WRITE;
  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  
  wr.wr.rdma.remote_addr = remote_addr;
  wr.wr.rdma.rkey = rkey;

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
      perror("error");
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
    }
    opts.qp_attr.srq = srq;
  }

  struct ibv_qp *qp = NULL;
  if(opts.native_qp){ // use native interface 
    struct ibv_qp_attr qp_attr;
    int qp_attr_mask;

    /*
    // the creation of CQ should be outside. It is here for debugging.
    struct ibv_cq * cq = ibv_create_cq(conn_id->verbs, 10, NULL, NULL, 0 );
    opts.qp_attr.send_cq = cq;
    opts.qp_attr.recv_cq = cq;*/

    assert(opts.qp_attr.send_cq != NULL && "for native QP, send CQ must be specified"); // see man ibv_create_cq
    assert(opts.qp_attr.recv_cq != NULL && "for native QP, receive CQ must be specified");
    assert(this->id_->pd != NULL);

    if(opts.inline_recv != 0){
      printf("The code is not tested. Try to use inline recv %u \n",opts.inline_recv);
      struct ibv_qp_init_attr_ex attr_ex;
      struct mlx5dv_qp_init_attr attr_dv;
      memset(&attr_ex, 0, sizeof(attr_ex));
      memset(&attr_dv, 0, sizeof(attr_dv));

    
      attr_ex.pd = this->id_->pd;
      attr_ex.comp_mask =  IBV_QP_INIT_ATTR_PD;
      attr_ex.qp_type = opts.qp_attr.qp_type;
      attr_ex.send_cq = opts.qp_attr.send_cq;
      attr_ex.recv_cq = opts.qp_attr.recv_cq;
      attr_ex.srq = opts.qp_attr.srq;
      attr_ex.cap.max_send_wr = opts.qp_attr.cap.max_send_wr;
      attr_ex.cap.max_recv_wr = opts.qp_attr.cap.max_recv_wr;
      attr_ex.cap.max_send_sge = opts.qp_attr.cap.max_send_sge;
      attr_ex.cap.max_recv_sge = opts.qp_attr.cap.max_recv_sge;
      attr_ex.cap.max_inline_data = opts.qp_attr.cap.max_inline_data;

      attr_dv.comp_mask = MLX5DV_QP_INIT_ATTR_MASK_QP_CREATE_FLAGS;
      attr_dv.create_flags = MLX5DV_QP_CREATE_ALLOW_SCATTER_TO_CQE;

      //qp = mlx5dv_create_qp(conn_id->verbs, &attr_ex,&attr_dv); // for correct linking should be added -lmlx5
    } else {
      qp = ibv_create_qp(this->id_->pd , &opts.qp_attr);
    }

    if(!qp){
      perror("ERROR");
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " creating native qp");
    }
 
    // INIT state
    qp_attr.qp_state = IBV_QPS_INIT;
    ret = rdma_init_qp_attr(conn_id, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " getting attr qp on INIT");
    ret = ibv_modify_qp(qp, &qp_attr, qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " setting attr qp to INIT");

    // RTR state 
    qp_attr.qp_state = IBV_QPS_RTR;
    ret = rdma_init_qp_attr(conn_id, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " getting attr qp on RTR");
    ret = ibv_modify_qp(qp, &qp_attr, qp_attr_mask);

    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " setting attr qp to RTR");
    qp_attr.qp_state = IBV_QPS_RTS;
    ret = rdma_init_qp_attr(conn_id, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " getting attr qp on RTS");
    ret = ibv_modify_qp(qp, &qp_attr, qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " setting attr qp to RTS");

    conn_param.qp_num = qp->qp_num;
    printf("create qp %u\n",qp->qp_num);
    

  } else { // use RDMA CM for creating a QP
    ret = rdma_create_qp(conn_id, this->id_->pd, &opts.qp_attr);
    if (ret) {
      // TODO(Fischi) Map error codes
      perror("ERROR");
      return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " getting creating qp");
    }
  }
  ret = rdma_accept(conn_id, &conn_param);
  if (ret) {
    perror("ERROR");
    return Status(StatusCode::Internal, "accept: error " + std::to_string(ret) + " accepting connection");
  }

  conn_id->srq = opts.qp_attr.srq;

  
  Endpoint *ep = new Endpoint(conn_id, private_data, private_data_len);
  if(qp) ep->SetQp(qp); // it is not good as we need to destroy it. Endpoint should have fields for self-created objects.

  return ep;
}

Listener::Listener(rdma_cm_id *id) : id_(id) {
}

Listener::~Listener(){
}

ibv_pd *Listener::GetPd(){
  assert(this->id_->pd != nullptr && "PD has not been created");
  return this->id_->pd;
}
ibv_context *Listener::GetContext(){
  return this->id_->verbs;
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


#ifdef WITH_MANUAL_RDMACM  // to manage events and connection manually
  struct rdma_cm_event *event;

  struct rdma_event_channel * cm_channel = rdma_create_event_channel();
  if (cm_channel == NULL) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating event channel");
  }

  ret = rdma_create_id(cm_channel, &id, NULL, RDMA_PS_TCP);
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating id endpoint");
  }

  ret = rdma_resolve_addr(id, NULL, addrinfo->ai_dst_addr, 2000);
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " resolving addresses to the remote endpoint");
  }
 
  ret = rdma_get_cm_event(cm_channel, &event);
  if(event->event != RDMA_CM_EVENT_ADDR_RESOLVED){
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating id endpoint");
  }
  rdma_ack_cm_event(event);

  ret = rdma_resolve_route(id, 2000);
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " resolving route to the remote endpoint");
  }

  ret = rdma_get_cm_event(cm_channel, &event);
  if(event->event != RDMA_CM_EVENT_ROUTE_RESOLVED){
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating id endpoint");
  }
  assert(event->id == id && "Unexpected route resolved event");
  rdma_ack_cm_event(event);

  id->channel = cm_channel;
#else 

  // setup endpoint, also can create qp
  ret = rdma_create_ep(&id, addrinfo, opts.pd, NULL); 
  if (ret) {
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " creating endpoint");
  }
   
#endif

  assert(id->pd != NULL && "pd is null");

  if (opts.use_srq && opts.qp_attr.srq == nullptr){
    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.attr.max_sge = opts.qp_attr.cap.max_recv_sge;
    srq_init_attr.attr.max_wr = opts.qp_attr.cap.max_recv_wr;
    auto srq = ibv_create_srq(id->pd, &srq_init_attr);
    if (srq == nullptr){
      return Status(StatusCode::Internal, "error " + std::to_string(errno) + " creating ibv_srq");
    }
  }

  struct ibv_qp * qp;
  if(opts.native_qp){
    /*
    // the creation of CQ should be outside. It is here for debugging.
    struct ibv_cq * cq = ibv_create_cq(id->verbs, 10, NULL, NULL, 0 );
    opts.qp_attr.send_cq = cq;
    opts.qp_attr.recv_cq = cq;*/

    assert(opts.qp_attr.send_cq != NULL && "for native QP, send CQ must be specified"); // see man ibv_create_cq
    assert(opts.qp_attr.recv_cq != NULL && "for native QP, receive CQ must be specified");
    assert(id->pd != NULL);

    qp = ibv_create_qp(id->pd , &opts.qp_attr);
  } else {
    ret = rdma_create_qp(id, id->pd, &opts.qp_attr);
    if (ret) {
      return Status(StatusCode::Internal, "dial: error creating qp");
    }
  }


  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  Endpoint *ep = new Endpoint(id);
  if(qp) ep->SetQp(qp);

  return ep;
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

  if(opts.native_qp){
    conn_param.qp_num = this->qp_->qp_num;
    this->id_->qp = NULL; // we need to NULL it here as connect uses this information
  }
 

  // connect to remote
  int ret = rdma_connect(this->id_, &conn_param);
  if (ret) {
    perror("ERROR");
    return Status(StatusCode::Internal, "Error " + std::to_string(ret) + " connecting to remote");
  }

#ifdef WITH_MANUAL_RDMACM
  // code in case events are handled manually.
  struct rdma_cm_event *event;
  ret = rdma_get_cm_event(this->id_->channel, &event);
  if(event->event != RDMA_CM_EVENT_CONNECT_RESPONSE && event->event != RDMA_CM_EVENT_ESTABLISHED){
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) + " on rdma connect");
  }
  rdma_ack_cm_event(event);
#endif

  // Set connection data
  size_t private_data_len = this->id_->event->param.conn.private_data_len;
  void * private_data = nullptr;
  if (private_data_len) {
    private_data = calloc(1, private_data_len);
    memcpy(private_data, this->id_->event->param.conn.private_data, private_data_len);
  }
  this->private_data_ = private_data;
  this->private_data_len_ = private_data_len; 

  if(opts.native_qp){ 
    struct ibv_qp_attr qp_attr;
    int qp_attr_mask;

    // INIT state
    qp_attr.qp_state = IBV_QPS_INIT;
    ret = rdma_init_qp_attr(this->id_, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " getting attr qp on INIT");
    ret = ibv_modify_qp(this->qp_, &qp_attr, qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " setting attr qp to INIT");

    // RTR state 
    qp_attr.qp_state = IBV_QPS_RTR;
    ret = rdma_init_qp_attr(this->id_, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " getting attr qp on RTR");
    ret = ibv_modify_qp(this->qp_, &qp_attr, qp_attr_mask);

    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " setting attr qp to RTR");
    qp_attr.qp_state = IBV_QPS_RTS;
    ret = rdma_init_qp_attr(this->id_, &qp_attr, &qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " getting attr qp on RTS");
    ret = ibv_modify_qp(this->qp_, &qp_attr, qp_attr_mask);
    if (ret)
      return Status(StatusCode::Internal, "connect: error " + std::to_string(ret) + " setting attr qp to RTS");

    ret = rdma_establish(this->id_);
    if (ret) {
      perror("ERROR");
      return Status(StatusCode::Internal, "Error " + std::to_string(ret) + " connecting to remote using rdma_establish");
    }

    this->id_->qp =  this->qp_; // for compatibility with the rest of the code
  }
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
  struct rdma_cm_id *id;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;
  hints.ai_flags = RAI_PASSIVE;

  ret = get_rdma_addr(ip.c_str(), NULL, std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "Error getting address info");
  }
 
  // setup endpoint
  ret = rdma_create_ep(&id, addrinfo, NULL, NULL); 
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "Error " + std::to_string(errno) +" creating listening endpoint");
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  ret = rdma_listen(id, 20);
  if(ret){
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Internal, "listening failed");
  }

  assert(id->pd!=nullptr && "pd is null");

  return new Listener(id);
}

}
}
