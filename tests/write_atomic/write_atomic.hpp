
#ifndef KNY_WRITE_ATOMIC_HPP
#define KNY_WRITE_ATOMIC_HPP

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
#include "receive_queue.hpp"

#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

struct write_atomic_meta {
  uint64_t head;
  uint64_t tail;
};


class WriteAtomicConnection : public Connection {
  public:
    WriteAtomicConnection(std::unique_ptr<endpoint::Endpoint>, struct ibv_mr *buf_mr, struct ibv_mr *buf_meta_mr, 
        std::shared_ptr<kym::endpoint::SharedReceiveQueue> rq, std::shared_ptr<memory::Allocator>, 
        struct ibv_mr *rbuf_head_mr, uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_,  
        uint64_t rbuf_meta_vaddr_, uint32_t rbuf_meta_rkey_);
    ~WriteAtomicConnection() = default;

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);
  private:
    std::unique_ptr<endpoint::Endpoint> ep_;
    std::shared_ptr<memory::Allocator> allocator_;

    std::shared_ptr<kym::endpoint::SharedReceiveQueue> srq_;

    // Local Buffer
    struct ibv_mr     *buf_mr_;
    struct ibv_mr     *buf_meta_mr_;   
    write_atomic_meta *buf_meta_;   
    size_t             buf_size_;

    // Remote Buffer
    uint64_t rbuf_vaddr_;
    uint32_t rbuf_rkey_;
    // Remote Buffer info
    uint64_t           rbuf_meta_vaddr_;
    uint32_t           rbuf_meta_rkey_;
    struct ibv_mr     *rbuf_meta_mr_; // Buffer to read into
    write_atomic_meta *rbuf_meta_;   

    uint32_t rbuf_size_;
};

StatusOr<std::unique_ptr<WriteAtomicConnection>> DialWriteAtomic(std::string ip, int port);

class WriteAtomicListener {
  public:
    WriteAtomicListener(std::unique_ptr<endpoint::Listener> listener);
    ~WriteAtomicListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<WriteAtomicConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
};

StatusOr<std::unique_ptr<WriteAtomicListener>> ListenWriteAtomic(std::string ip, int port);



}
}

#endif // KNY_WRITE_ATOMIC_HPP
