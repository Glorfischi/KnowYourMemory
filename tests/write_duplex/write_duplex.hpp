
#ifndef KNY_WRITE_DUPLEX_HPP_
#define KNY_WRITE_DUPLEX_HPP_

#include <stddef.h>
#include <string>
#include <vector>
#include <memory> // For smart pointers


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"
#include "receive_queue.hpp"

#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

class WriteDuplexConnection : public Receiver {
  public:
    WriteDuplexConnection(std::unique_ptr<endpoint::Endpoint>, struct ibv_mr *buf_mr, struct ibv_mr *buf_head_mr, 
        std::unique_ptr<kym::endpoint::ReceiveQueue> rq, std::shared_ptr<memory::Allocator>, 
        struct ibv_mr *rbuf_head_mr, uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_,  uint64_t rbuf_head_vaddr_, 
        uint32_t rbuf_head_rkey_);
    ~WriteDuplexConnection() = default;

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);
  private:
    std::unique_ptr<endpoint::Endpoint> ep_;
    std::unique_ptr<kym::endpoint::ReceiveQueue> rq_;
    std::shared_ptr<memory::Allocator> allocator_;

    // Local Buffer
    struct ibv_mr *buf_mr_;
    struct ibv_mr *buf_head_mr_;   
    uint32_t      *buf_head_;   
    size_t        buf_size_;


    // Remote Buffer
    uint64_t rbuf_vaddr_;
    uint32_t rbuf_rkey_;
    // Remote Buffer info
    uint64_t       rbuf_head_vaddr_;
    uint32_t       rbuf_head_rkey_;
    struct ibv_mr *rbuf_head_mr_; // Buffer to read into
    uint32_t      *rbuf_head_;   

    uint32_t rbuf_tail_;
    uint32_t rbuf_size_;
    bool     rbuf_full_;
};

StatusOr<std::unique_ptr<WriteDuplexConnection>> DialWriteDuplex(std::string ip, int port);

class WriteDuplexListener {
  public:
    WriteDuplexListener(std::unique_ptr<endpoint::Listener> listener);
    ~WriteDuplexListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<WriteDuplexConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
};

StatusOr<std::unique_ptr<WriteDuplexListener>> ListenWriteDuplex(std::string ip, int port);



}
}

#endif // KNY_WRITE_DUPLEX_HPP_
