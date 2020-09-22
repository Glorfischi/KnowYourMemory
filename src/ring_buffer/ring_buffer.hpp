#ifndef KNY_RINGBUFFER_HPP_
#define KNY_RINGBUFFER_HPP_

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <list>

#include <infiniband/verbs.h> 

#include "error.hpp"

namespace kym {
namespace ringbuffer {

/*
 * Ring Buffer for RDMA connection
 */
struct BufferContext {
  uint32_t data[4];
};

class Buffer {
  public:
    virtual ~Buffer() = default;

    // Deregisters MR and frees memory
    virtual Status Close() = 0;

    // Returns a Context with all infomation necessary for the setup of a remote buffer
    virtual BufferContext GetContext() = 0;
    // Returns the current read pointer without updating it
    // There is always at least 4 bytes of usable space directly after the read pointer
    virtual void *GetReadPtr() = 0;

    // Reads len bytes and updates the read pointer
    virtual void *Read(uint32_t len) = 0;
    // Frees the memory region and returns the new header position
    virtual uint32_t Free(void *addr) = 0; // TODO(fischi) I guess this might fail?
};

class RemoteBuffer {
  public:
    virtual ~RemoteBuffer() = default;

    // Updates the head position to the new value
    virtual void UpdateHead(uint32_t head) = 0;
    
    // Return the rkey of the MR
    virtual uint32_t GetKey() = 0;
    // Returns the current tail  
    // There is always at least 4 bytes of usable space directly after the tail
    virtual uint32_t GetTail() = 0;
    // Returns the write address if you want to write len bytes to the buffer without updating the tail
    // Can return an error if there is not enough free space
    virtual StatusOr<uint64_t>GetWriteAddr(uint32_t len) = 0;
    // Returns the write address if you want to write len bytes to the buffer and updates the tail accordingly
    // Can return an error if there is not enough free space
    virtual StatusOr<uint64_t>Write(uint32_t len) = 0;

};

// simple ring buffer that jumps to the front of the buffer if there is not enough space to contigously write 
// NOT THREAD SAFE
class BasicRingBuffer : public Buffer {
  public:
    Status Close();

    BufferContext GetContext();
    void *GetReadPtr();

    void *Read(uint32_t len);
    uint32_t Free(void *addr);
  private:
    uint32_t add(uint32_t offset, uint32_t length);

    struct ibv_mr *mr_;
    void *addr_;

    uint32_t head_;
    std::list<uint32_t> outstanding_;
    
    uint32_t read_ptr_;
    uint32_t length_;

};
class BasicRemoteBuffer : public RemoteBuffer {
  public:
    void UpdateHead(uint32_t head);
    
    uint32_t GetKey();
    uint32_t GetTail();

    StatusOr<uint64_t>GetWriteAddr(uint32_t len);
    StatusOr<uint64_t>Write(uint32_t len);
  private:
    struct ibv_mr *mr_;

    uint64_t addr_;
    uint32_t length_;
    uint32_t head_;
    uint32_t tail_;
    bool full_;

};


class MagicRingBuffer : public Buffer {
  public:
    MagicRingBuffer(struct ibv_mr *mr);
    Status Close();

    BufferContext GetContext();
    void *GetReadPtr();

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
StatusOr<MagicRingBuffer*> NewMagicRingBuffer(struct ibv_pd *pd, uint32_t size);

class MagicRemoteBuffer : public RemoteBuffer {
  public:
    MagicRemoteBuffer(BufferContext ctx);

    void UpdateHead(uint32_t head);
    
    uint32_t GetKey();
    uint32_t GetTail();

    StatusOr<uint64_t>GetWriteAddr(uint32_t len);
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

#endif // KNY_RINGBUFFER_HPP_
