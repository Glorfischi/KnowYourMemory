
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
    WriteDuplexConnection(std::shared_ptr<endpoint::Endpoint>, struct ibv_mr *buf_mr, 
        std::shared_ptr<kym::endpoint::IReceiveQueue> rq, std::shared_ptr<memory::Allocator>, 
        uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_);
    ~WriteDuplexConnection() = default;

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;
    std::shared_ptr<kym::endpoint::IReceiveQueue> rq_;
    std::shared_ptr<memory::Allocator> allocator_;

    // Local Buffer
    struct ibv_mr *buf_mr_;
    uint32_t      buf_head_;   


    // Remote Buffer
    uint64_t rbuf_vaddr_;
    uint32_t rbuf_rkey_;

    uint32_t rbuf_head_;   
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
