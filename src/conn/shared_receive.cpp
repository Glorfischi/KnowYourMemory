#include "shared_receive.hpp"

#include <assert.h>

#include <cstddef>
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
#include "conn/send_receive.hpp"


namespace kym {
namespace connection {

/*
 * Client Dial
 */

int DialSharedReceive(SharedReceiveConnection **conn, std::string ip, int port) {
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

  // TODO(fischi): SRQ
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
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;

  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 0;  // The maximum number of outstanding RDMA read and atomic operations that 
  // the local side will accept from the remote side.
  conn_param.initiator_depth =  0;  // The maximum number of outstanding RDMA read and atomic operations that the 
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
  SendReceiveSender *sender = new SendReceiveSender(id, allocator);
  
  // Init Receiver
  SharedReceiver *receiver = new SharedReceiver(id);

  *conn = new SharedReceiveConnection(sender, receiver);
  return ret;
}

SharedReceiveConnection *DialSharedReceive(std::string ip, int port) {
  SharedReceiveConnection *conn;
  int ret = DialSharedReceive(&conn, ip, port);
  if(ret){
    std::cout << "ERROR " << ret << std::endl;
  };
  return conn;
}

/* 
 * Server listener
 */
int ListenSharedReceive(SharedReceiveListener **ln, std::string ip, int port) {
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
  *ln = new SharedReceiveListener(id);
  return 0;
}

SharedReceiveListener *ListenSharedReceive(std::string ip, int port) {
  SharedReceiveListener *ln;
  ListenSharedReceive(&ln, ip, port);
  return ln;
}

SharedReceiveConnection *SharedReceiveListener::Accept() {
  int ret;
  struct rdma_cm_id *conn_id;

  // TODO(fischi): Error handling
  ret = rdma_get_request(this->ln_id_, &conn_id);

  
  // TODO(fischi): SRQ
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
  attr.cap.max_inline_data = 0;
  attr.qp_type = IBV_QPT_RC;

  struct rdma_conn_param conn_param;
  memset(&conn_param, 0 , sizeof conn_param);
  conn_param.responder_resources = 0;  // The maximum number of outstanding RDMA read and atomic operations that 
  // the local side will accept from the remote side.
  conn_param.initiator_depth =  0;  // The maximum number of outstanding RDMA read and atomic operations that the 
  // local side will have to the remote side.
  conn_param.retry_count = 3;  
  conn_param.rnr_retry_count = 8; 


  // Creating shared receive queue if none is set
  // TODO(fischi): Parameterize
  if (this->srq_ == NULL) {
    struct ibv_srq_init_attr srq_init_attr;
    srq_init_attr.attr.max_sge = 1;
    srq_init_attr.attr.max_wr = 10;
    this->srq_ = ibv_create_srq(this->ln_id_->pd, &srq_init_attr);
  }

  attr.srq = this->srq_;
  
  ret = rdma_create_qp(conn_id, this->ln_id_->pd, &attr);

  ret = rdma_accept(conn_id, &conn_param);
  if(ret) {
    std::cerr << "Error accepting " << ret << std::endl;
  }

  // Init Sender
  // TODO(fischi): Free
  memory::DumbAllocator *allocator = new memory::DumbAllocator(conn_id->pd);
  SendReceiveSender *sender = new SendReceiveSender(conn_id, allocator);
  
  // Init Receiver
  SharedReceiver *receiver = new SharedReceiver(conn_id);

  std::cout << "Accept" << std::endl;
  return new SharedReceiveConnection(sender, receiver);
}

SharedReceiveListener::SharedReceiveListener(struct rdma_cm_id *id): ln_id_(id){
}

SharedReceiveListener::~SharedReceiveListener(){
  rdma_destroy_ep(this->ln_id_);
  if (this->srq_ != NULL){
    ibv_destroy_srq(this->srq_);
  }
}


/*
 * SharedReceiveConnection
 */
SharedReceiveConnection::SharedReceiveConnection(SendReceiveSender *sender, SharedReceiver *receiver): sender_(sender), receiver_(receiver){
}
SharedReceiveConnection::~SharedReceiveConnection(){
  delete this->receiver_;
  delete this->sender_;
}

kym::memory::Region SharedReceiveConnection::GetMemoryRegion(size_t size){
  return this->sender_->GetMemoryRegion(size);
}

int SharedReceiveConnection::Send(kym::memory::Region region){
  return this->sender_->Send(region);
}

void SharedReceiveConnection::Free(kym::memory::Region region){
  return this->sender_->Free(region);
}

kym::connection::ReceiveRegion SharedReceiveConnection::Receive() {
  return this->receiver_->Receive();
}

void SharedReceiveConnection::Free(kym::connection::ReceiveRegion region){
  return this->receiver_->Free(region);
}



/*
 * SendReceiveReceiver
 */
SharedReceiver::SharedReceiver(struct rdma_cm_id * id): id_(id), qp_(id->qp) {
  // TODO(fischi) Should be shared
  // TODO(fischi) Parameterize
  size_t transfer_size = 1048576;
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

SharedReceiver::~SharedReceiver(){
  rdma_destroy_ep(this->id_);
}

kym::connection::ReceiveRegion SharedReceiver::Receive() {
  struct ibv_wc wc;
  while(ibv_poll_cq(this->qp_->recv_cq, 1, &wc) == 0){}
  //TODO(fischi): Error handling
  //TODO(fischi): Handle if id is not in range?
  kym::connection::ReceiveRegion reg = {0};
  reg.id = wc.wr_id;
  reg.addr = this->mrs_[wc.wr_id]->addr;
  reg.length = wc.byte_len;
  return reg;
}

void SharedReceiver::Free(kym::connection::ReceiveRegion region) {
  // Repost MR
  assert((size_t)region.id < this->mrs_.size());
  struct ibv_mr *mr = this->mrs_[region.id];

  struct ibv_sge sge;
  sge.addr = (uintptr_t)mr->addr;
  sge.length = mr->length;
  sge.lkey = mr->lkey;

  struct ibv_recv_wr wr, *bad;
  wr.wr_id = region.id;
  wr.next = NULL;
  wr.sg_list = &sge;
  wr.num_sge = 1;

  int ret = ibv_post_recv(this->qp_, &wr, &bad);
  if (ret) {
    std::cout << "ERROR " << ret << std::endl;
  }
  //TODO(fischi): Error handling
  return;
}

}
}
