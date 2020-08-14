
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


class WriteAtomicSender : public Sender {
  public:
    WriteAtomicSender(std::unique_ptr<endpoint::Endpoint>, std::shared_ptr<memory::Allocator>, 
        uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_,  
        uint64_t rbuf_meta_vaddr_, uint32_t rbuf_meta_rkey_,
        struct ibv_mr *rbuf_meta_mr);
    ~WriteAtomicSender() = default;

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);
  private:
    std::unique_ptr<endpoint::Endpoint> ep_;
    std::shared_ptr<memory::Allocator> allocator_;
   
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


StatusOr<std::unique_ptr<WriteAtomicSender>> DialWriteAtomic(std::string ip, int port);

class WriteAtomicListener : Receiver {
  public:
    WriteAtomicListener(std::unique_ptr<endpoint::Listener> listener,
      struct ibv_mr     *buf_mr,
      struct ibv_mr     *buf_meta_mr,
      struct ibv_mr     *rcv_mr,
      uint32_t rcv_bufs_count);
    ~WriteAtomicListener() = default;

    Status Close();

    Status Accept();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    std::unique_ptr<endpoint::Listener> listener_;
    std::vector<std::unique_ptr<endpoint::Endpoint>> eps_;

    // Receive buffers
    struct ibv_mr *rcv_mr_;
    char*          rcv_bufs_;
    uint32_t       rcv_bufs_count_;

     // Local Buffer
    struct ibv_mr     *buf_mr_;
    struct ibv_mr     *buf_meta_mr_;   
    volatile write_atomic_meta *buf_meta_;   
    size_t             buf_size_;

};

StatusOr<std::unique_ptr<WriteAtomicListener>> ListenWriteAtomic(std::string ip, int port);



}
}

#endif // KNY_WRITE_ATOMIC_HPP
