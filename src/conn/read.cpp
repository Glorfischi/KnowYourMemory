#include "read.hpp"

#include <assert.h>

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <ostream>
#include <string>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "kym/conn.hpp"
#include "kym/mm.hpp"
#include "mm/dumb_allocator.hpp"


namespace kym {
namespace connection {

/*
 * Client Dial
 */

int DialRead(ReadConnection **conn, std::string ip, int port) {
  int ret = 0;

  // Setup RDMA Endpoint and connect to remote
  assert(!ip.empty());

  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  

  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;

  ret = rdma_getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    return ret;
  } 

  struct rdma_cm_id *id;
  struct ibv_qp_init_attr attr;
  struct rdma_conn_param conn_param;

  // TODO(fischi): Parameterize
  memset(&attr, 0, sizeof attr);
  attr.cap.max_send_wr = 1;  // The maximum number of outstanding Work Requests that can be
  // posted to the Send Queue in that Queue Pair
  attr.cap.max_recv_wr = 10;  // The maximum number of outstanding Work Requests that can be
  // posted to the Receive Queue in that Queue Pair. 
  attr.cap.max_send_sge = 1; // The maximum number of scatter/gather elements in any Work 
  // Request that can be posted to the Send Queue in that Queue Pair.
  attr.cap.max_recv_sge = 1; // The maximum number of scatter/gather elements in any 
  // Work Request that can be posted to the Receive Queue in that Queue Pair. 
  attr.cap.max_inline_data = 16;
  attr.qp_type = IBV_QPT_RC;

  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 5;  // The maximum number of outstanding RDMA read and atomic operations that 
  // the local side will accept from the remote side.
  conn_param.initiator_depth =  5;  // The maximum number of outstanding RDMA read and atomic operations that the 
  // local side will have to the remote side.
  conn_param.retry_count = 100;  
  conn_param.rnr_retry_count = 100; 


  // setup endpoint, also creates qp(?)
  ret = rdma_create_ep(&id, addrinfo, NULL, &attr); 
  if (ret) {
    return ret;
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);


  // connect to remote
  ret = rdma_connect(id, &conn_param);
  if (ret) {
    return ret;
  }

  // Init Sender
  // TODO(fischi): Free
  memory::DumbAllocator *allocator = new memory::DumbAllocator(id->pd);
  ReadSender *sender = new ReadSender(id, allocator);
  
  // Init Receiver
  ReadReceiver *receiver = new ReadReceiver(id, allocator);

  *conn = new ReadConnection(sender, receiver);
  return ret;
}

ReadConnection *DialRead(std::string ip, int port) {
  ReadConnection *conn;
  int ret = DialRead(&conn, ip, port);
  if(ret){
    std::cout << "ERROR " << ret << std::endl;
  };
  return conn;
}

/* 
 * Server listener
 */
int ListenRead(ReadListener **ln, std::string ip, int port) {
  int ret = 0;

  // Setup RDMA Endpoint and connect to remote
  assert(!ip.empty());

  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }

  struct rdma_addrinfo hints;
  struct rdma_addrinfo *addrinfo;

  memset(&hints, 0, sizeof hints);
  hints.ai_port_space = RDMA_PS_TCP;
  hints.ai_flags = RAI_PASSIVE;

  ret = rdma_getaddrinfo(ip.c_str(), std::to_string(port).c_str(), &hints, &addrinfo);
  if (ret) {
    return ret;
  }
  
  struct rdma_cm_id *id;
  struct rdma_conn_param conn_param;
 
  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 0;  // The maximum number of outstanding RDMA read and atomic operations that 
  // the local side will accept from the remote side.
  conn_param.initiator_depth =  0;  // The maximum number of outstanding RDMA read and atomic operations that the 
  // local side will have to the remote side.
  conn_param.retry_count = 8;  
  conn_param.rnr_retry_count = 3; 

  // setup endpoint, also creates qp(?)
  ret = rdma_create_ep(&id, addrinfo, NULL, NULL); 
  if (ret) {
    return ret;
  }

  // cleanup addrinfo, we don't need it anymore
  rdma_freeaddrinfo(addrinfo);

  ret = rdma_listen(id, 2);
  if(ret){
    return ret;
  }
  *ln = new ReadListener(id);
  return 0;
}

ReadListener *ListenRead(std::string ip, int port) {
  ReadListener *ln;
  ListenRead(&ln, ip, port);
  return ln;
}

ReadConnection *ReadListener::Accept() {
  int ret;
  struct rdma_cm_id *conn_id;

  // TODO(fischi): Error handling
  ret = rdma_get_request(this->ln_id_, &conn_id);

  
  // TODO(fischi): Parameterize
  struct ibv_qp_init_attr attr;
  memset(&attr, 0, sizeof attr);
  attr.cap.max_send_wr = 1;  // The maximum number of outstanding Work Requests that can be
  // posted to the Send Queue in that Queue Pair
  attr.cap.max_recv_wr = 10;  // The maximum number of outstanding Work Requests that can be
  // posted to the Receive Queue in that Queue Pair.  // WAT(fischi) Seems to be infinit for >=8 ??!
  attr.cap.max_send_sge = 1; // The maximum number of scatter/gather elements in any Work 
  // Request that can be posted to the Send Queue in that Queue Pair.
  attr.cap.max_recv_sge = 1; // The maximum number of scatter/gather elements in any 
  // Work Request that can be posted to the Receive Queue in that Queue Pair. 
  attr.cap.max_inline_data = 16;
  attr.qp_type = IBV_QPT_RC;

  struct rdma_conn_param conn_param;
  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 5;  // The maximum number of outstanding RDMA read and atomic operations that 
  // the local side will accept from the remote side.
  conn_param.initiator_depth =  5;  // The maximum number of outstanding RDMA read and atomic operations that the 
  // local side will have to the remote side.
  conn_param.retry_count = 3;  
  conn_param.rnr_retry_count = 8; 


  ret = rdma_create_qp(conn_id, this->ln_id_->pd, &attr);

  ret = rdma_accept(conn_id, &conn_param);

  // Init Sender
  // TODO(fischi): Free
  memory::DumbAllocator *allocator = new memory::DumbAllocator(conn_id->pd);
  ReadSender *sender = new ReadSender(conn_id, allocator);
  
  // Init Receiver
  ReadReceiver *receiver = new ReadReceiver(conn_id, allocator);

  return new ReadConnection(sender, receiver);
}

ReadListener::ReadListener(struct rdma_cm_id *id): ln_id_(id){
}

ReadListener::~ReadListener(){
  rdma_destroy_ep(this->ln_id_);
}


/*
 * ReadConnection
 */
ReadConnection::ReadConnection(ReadSender *sender, ReadReceiver *receiver): sender_(sender), receiver_(receiver){
}
ReadConnection::~ReadConnection(){
  delete this->receiver_;
  delete this->sender_;
}

kym::memory::Region ReadConnection::GetMemoryRegion(size_t size){
  return this->sender_->GetMemoryRegion(size);
}

int ReadConnection::Send(kym::memory::Region region){
  return this->sender_->Send(region);
}

void ReadConnection::Free(kym::memory::Region region){
  return this->sender_->Free(region);
}

kym::connection::ReceiveRegion ReadConnection::Receive() {
  return this->receiver_->Receive();
}

void ReadConnection::Free(kym::connection::ReceiveRegion region){
  return this->receiver_->Free(region);
}



/*
 * ReadSender
 */

ReadSender::ReadSender(struct rdma_cm_id * id, kym::memory::Allocator *allocator): id_(id), qp_(id->qp), allocator_(allocator){
}
ReadSender::~ReadSender(){
  rdma_destroy_ep(this->id_);
}

kym::memory::Region ReadSender::GetMemoryRegion(size_t size){
  assert(this->allocator_ != NULL);
  return this->allocator_->Alloc(size);
};

void ReadSender::Free(kym::memory::Region region){
  assert(this->allocator_ != NULL);
  // TODO(Fischi): To behave similarly to other connections, we cannot free the sending region here. We need to think about freeing this.
  // IDEA(Fischi): Maybe have some kind of garbage collection round on the free call or when sending?
  return;
  return this->allocator_->Free(region);
}

int ReadSender::Send(kym::memory::Region region){
  struct ReadRequest req;
  req.addr = (uint64_t)region.addr;
  req.key = region.lkey;
  req.length = region.length;

  struct ibv_sge sge;
  sge.addr = (uintptr_t)&req;
  sge.length = sizeof(req);
  sge.lkey =  region.lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = 0;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_SEND;
  wr.send_flags = IBV_SEND_SIGNALED | IBV_SEND_INLINE;  

  ibv_post_send(this->qp_, &wr, &bad);

  struct ibv_wc wc;
  while(ibv_poll_cq(this->qp_->send_cq, 1, &wc) == 0){
    // ToDo(fischi): Add bursting
  }
  return wc.status;
}



/*
 * ReadReceiver
 */
ReadReceiver::ReadReceiver(struct rdma_cm_id * id, kym::memory::Allocator *allocator): id_(id), qp_(id->qp), allocator_(allocator) {
  // TODO(fischi) Parameterize
  size_t transfer_size = sizeof(ReadRequest);
  for (int i = 0; i < 10; i++){

    char* buf = (char*)malloc(transfer_size);
    struct ibv_mr * mr = ibv_reg_mr(id->pd, buf, transfer_size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);  

    struct ibv_sge sge;
    sge.addr = (uintptr_t)mr->addr;
    sge.length = transfer_size;
    sge.lkey = mr->lkey;

    struct ibv_recv_wr wr, *bad;
    wr.wr_id = i;
    wr.next = NULL;
    wr.sg_list = &sge;
    wr.num_sge = 1;

    int ret = ibv_post_recv(id->qp, &wr, &bad);
    if (ret) {
      std::cerr << "Error " << ret << std::endl;
    }
    this->mrs_.push_back(mr);
  }
}

ReadReceiver::~ReadReceiver(){
  rdma_destroy_ep(this->id_);
}

kym::connection::ReceiveRegion ReadReceiver::Receive() {
  struct ibv_wc wc;
  while(ibv_poll_cq(this->qp_->recv_cq, 1, &wc) == 0){}
  //TODO(fischi): Error handling
  //TODO(fischi): Handle if id is not in range?
  uint64_t mr_id = wc.wr_id;
  auto mr = this->mrs_[mr_id];


  ReadRequest *req = (ReadRequest *)mr->addr;
  // std::cout << "Got addr " << std::hex << req->addr << " key " << req->key << std::dec << " len " << req->length << std::endl;

  auto region = this->allocator_->Alloc(req->length);
  // TODO(fischi): We are currently leaking these receive buffers. Fixing this in a non hacky way, would involve rethinking memory regions.
  kym::connection::ReceiveRegion reg = {0};
  reg.addr = region.addr;
  reg.length = region.length;
  struct ibv_sge sge;

  sge.addr = (uintptr_t)region.addr;
  sge.length = region.length;
  sge.lkey =  region.lkey;
  struct ibv_send_wr wr, *bad;

  wr.wr_id = 0;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;
  wr.opcode = IBV_WR_RDMA_READ;
  wr.send_flags = IBV_SEND_SIGNALED;  
  wr.wr.rdma.remote_addr = req->addr;
  wr.wr.rdma.rkey = req->key;

  int ret = ibv_post_send(this->qp_, &wr, &bad);
  if (ret) {
    std::cout << "ERROR " << ret << std::endl;
  }

  while(ibv_poll_cq(this->qp_->send_cq, 1, &wc) == 0){}


  sge.addr = (uintptr_t)mr->addr;
  sge.length = mr->length;
  sge.lkey = mr->lkey;

  struct ibv_recv_wr rcv_wr, *rcv_bad;
  rcv_wr.wr_id = mr_id;
  rcv_wr.next = NULL;
  rcv_wr.sg_list = &sge;
  rcv_wr.num_sge = 1;

  ret = ibv_post_recv(this->qp_, &rcv_wr, &rcv_bad);
  if (ret) {
    std::cout << "ERROR " << ret << std::endl;
  }


  
  return reg;
}

void ReadReceiver::Free(kym::connection::ReceiveRegion region) {
  // TODO(fischi) Free recv buffer

  
  //TODO(fischi): Error handling
  return;
}

}
}
