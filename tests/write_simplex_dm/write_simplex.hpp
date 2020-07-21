
#ifndef KNY_WRITE_SIMPLEX_HPP_
#define KNY_WRITE_SIMPLEX_HPP_

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
    WriteSimplexReceiver(std::shared_ptr<endpoint::Endpoint>, struct ibv_mr *buf_mr, std::vector<struct ibv_mr*> rcv_mrs);
    ~WriteSimplexReceiver() = default;

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;

    std::vector<struct ibv_mr *> rcv_mrs_; // Posted buffers to receive events for incomming messages

    struct ibv_mr *buf_mr_;
    uint32_t      buf_head_;   
};

class WriteSimplexSender : public Sender {
  public:
    WriteSimplexSender(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<memory::Allocator>,
        std::vector<struct ibv_mr *> ack_mrs, uint64_t buf_vaddr_, uint32_t buf_rkey_);
    ~WriteSimplexSender() = default;

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;

    std::vector<struct ibv_mr *> ack_mrs_; // Posted buffers to receive read acknowledgements to
    int ack_outstanding_; // Number of outstaning acks. If equal to number of buffers, we cannot send until we receive


    std::shared_ptr<memory::Allocator> allocator_;

    uint64_t buf_vaddr_;
    uint32_t buf_rkey_;

    uint32_t buf_head_;   
    uint32_t buf_tail_;
    uint32_t buf_size_;
    bool     buf_full_;
};

// For now, client is sender and server is receiver.
StatusOr<std::unique_ptr<WriteSimplexSender>> DialWriteSimplex(std::string ip, int port);

class WriteSimplexListener {
  public:
    WriteSimplexListener(std::unique_ptr<endpoint::Listener> listener);
    ~WriteSimplexListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<WriteSimplexReceiver>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
};

StatusOr<std::unique_ptr<WriteSimplexListener>> ListenWriteSimplex(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
