#ifndef KNY_REVERSE_BUFFER_HPP
#define KNY_REVERSE_BUFFER_HPP

#include <cstddef>
#include <list>

#include <infiniband/verbs.h> 


#include "../error.hpp"

#include "ring_buffer.hpp"

namespace kym {
namespace ringbuffer {

/*
 *  Sending Ring Buffer for RDMA connection
 */


class SendBuffer  {
  public:
    SendBuffer(struct ibv_mr *mr);
    Status Close();

    BufferContext GetContext();

    void UpdateHead(uint32_t head);

    void *Write(uint32_t len);
  private:
    struct ibv_mr *mr_;
    void *addr_;

    uint32_t head_;
    uint32_t tail_;
    
    uint32_t read_ptr_;
    uint32_t length_;
};
StatusOr<SendBuffer*> NewSendBuffer(struct ibv_pd *pd, uint32_t size);

class SendRemoteBuffer {
  public:
    SendRemoteBuffer(BufferContext ctx);

    uint32_t GetKey();

    uint64_t GetReadPtr(uint32_t len);
    uint64_t Read(uint32_t len);
    // Returns the new head
    uint32_t Free(void *addr);
  private:
    uint64_t addr_;
    uint32_t length_;
    uint32_t key_;

    uint32_t head_;
    std::list<uint32_t> outstanding_;
    uint32_t tail_;
    bool full_;

};

}
}

#endif // KNY_REVERSE_BUFFER_HPP
