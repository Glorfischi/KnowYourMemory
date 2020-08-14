
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_ENDPOINT_HPP
#define KNY_ENDPOINT_HPP

#include <bits/stdint-uintn.h>
#include <memory> // For smart pointers

#include <rdma/rdma_cma.h>
#include <infiniband/verbs.h> 
#include <rdma/rdma_verbs.h>

#include "error.hpp"

namespace kym {
namespace endpoint {

struct Options {
  struct ibv_pd *pd;
  struct ibv_qp_init_attr  qp_attr;
  
  bool use_srq; // Wether to use a shared receive queue for receiving. If so and no srq is set in the qp_attr, one will be created using the corresponding capabilities of the qp_attr

  const void *private_data;
	uint8_t private_data_len;
	uint8_t responder_resources;
	uint8_t initiator_depth;
	uint8_t flow_control;
	uint8_t retry_count;		/* ignored when accepting */
	uint8_t rnr_retry_count;
};


class Endpoint {
  public:
    Endpoint(rdma_cm_id*);
    ~Endpoint();

    Status Close();

    ibv_pd GetPd();
    ibv_srq *GetSRQ();

    Status PostSendRaw(struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr);
    Status PostSend(uint64_t ctx, uint32_t lkey, void *addr, size_t size);
    Status PostInline(uint64_t ctx, void *addr, size_t size);
    Status PostRead(uint64_t ctx, uint32_t lkey, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey);
    Status PostWrite(uint64_t ctx, uint32_t lkey, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey);
    Status PostWriteWithImmidate(uint64_t ctx, uint32_t lkey, void *addr, size_t size, uint64_t remote_addr, uint32_t rkey, uint32_t imm);
    Status PostImmidate(uint64_t ctx, uint32_t imm);
    StatusOr<struct ibv_wc> PollSendCq();

    Status PostRecvRaw(struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr);
    Status PostRecv(uint64_t ctx, uint32_t lkey, void *addr, size_t size);
    StatusOr<struct ibv_wc> PollRecvCq();
    StatusOr<struct ibv_wc> PollRecvCqOnce();
  private:
    rdma_cm_id *id_;
};

class Listener {
  public:
    Listener(rdma_cm_id*);
    ~Listener();

    Status Close();

    ibv_pd GetPd();

    StatusOr<std::unique_ptr<Endpoint>> Accept(Options opts);
  private:
    rdma_cm_id *id_;
};

StatusOr<std::unique_ptr<Endpoint>> Dial(std::string ip, int port, Options opts);
StatusOr<std::unique_ptr<Listener>> Listen(std::string ip, int port);

}
}

#endif // KNY_ENDPOINT_HPP
