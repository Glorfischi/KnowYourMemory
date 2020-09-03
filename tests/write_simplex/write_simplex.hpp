
#ifndef KNY_WRITE_SIMPLEX_HPP_
#define KNY_WRITE_SIMPLEX_HPP_

#include <bits/stdint-uintn.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory> // For smart pointers


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"

#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

class WriteSimplexReceiver : public Receiver {
  public:
    WriteSimplexReceiver(endpoint::Endpoint *, struct ibv_mr *buf_mr, std::vector<struct ibv_mr*> rcv_mrs);
    ~WriteSimplexReceiver();

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    endpoint::Endpoint *ep_;

    std::vector<struct ibv_mr *> rcv_mrs_; // Posted buffers to receive events for incomming messages

    struct ibv_mr *buf_mr_;
    uint32_t      buf_head_;   
    uint32_t      buf_ack_head_;   
    uint32_t      buf_max_unack_;
};

class WriteSimplexSender : public Sender, public BatchSender {
  public:
    WriteSimplexSender(endpoint::Endpoint *, memory::Allocator *,
        std::vector<struct ibv_mr *> ack_mrs, uint64_t buf_vaddr_, uint32_t buf_rkey_);
    ~WriteSimplexSender();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    Status Free(SendRegion region);
  private:
    endpoint::Endpoint *ep_;

    std::vector<struct ibv_mr *> ack_mrs_; // Posted buffers to receive read acknowledgements to
    int ack_outstanding_; // Number of outstaning acks. If equal to number of buffers, we cannot send until we receive


    memory::Allocator *allocator_;

    uint64_t buf_vaddr_;
    uint32_t buf_rkey_;

    uint32_t buf_head_;   
    uint32_t buf_tail_;
    uint32_t buf_size_;
    bool     buf_full_;
};

// For now, client is sender and server is receiver.
StatusOr<WriteSimplexSender *> DialWriteSimplex(std::string ip, int port);
StatusOr<WriteSimplexSender *> DialWriteSimplex(std::string ip, int port, std::string src);

class WriteSimplexListener {
  public:
    WriteSimplexListener(endpoint::Listener *listener);
    ~WriteSimplexListener();

    Status Close();

    StatusOr<WriteSimplexReceiver *> Accept();
  private:
    endpoint::Listener *listener_;
};

StatusOr<WriteSimplexListener *> ListenWriteSimplex(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
