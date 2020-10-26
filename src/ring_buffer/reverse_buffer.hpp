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
 * Reversed Ring Buffer for RDMA connection
 */


class ReverseRingBuffer : public Buffer {
  public:
    ReverseRingBuffer(struct ibv_mr *mr);
    Status Close();

    BufferContext GetContext();
    void *GetReadPtr();
    uint32_t GetReadOff();

    void *Read(uint32_t len);
    uint32_t Free(void *addr);
  private:
    struct ibv_mr *mr_;
    void *addr_;

    uint32_t head_;
    std::list<uint32_t> outstanding_;
    
    uint32_t read_ptr_;
    uint32_t length_;
};
StatusOr<ReverseRingBuffer*> NewReverseRingBuffer(struct ibv_pd *pd, uint32_t size);

class ReverseRemoteBuffer : public RemoteBuffer {
  public:
    ReverseRemoteBuffer(BufferContext ctx);

    void UpdateHead(uint32_t head);
    
    uint32_t GetKey();
    uint32_t GetTail();

    StatusOr<uint64_t>GetWriteAddr(uint32_t len);
    StatusOr<uint32_t>GetNextTail(uint32_t len);
    StatusOr<uint64_t>Write(uint32_t len);
  private:

    uint64_t addr_;
    uint32_t length_;
    uint32_t key_;

    uint32_t head_;
    uint32_t tail_;
    bool full_;

};

}
}

#endif // KNY_REVERSE_BUFFER_HPP
