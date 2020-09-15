#include "error.hpp"
#include <bits/stdint-uintn.h>
#include <cstddef>

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
    virtual void *GetReadPtr();

    // Reads len bytes and updates the read pointer
    virtual void *Read(uint32_t len) = 0;
    // Frees the memory region and returns the new header position
    virtual uint32_t Free(void *addr, uint32_t len) = 0; // TODO(fischi) I guess this might fail?
};

class RemoteBuffer {
  public:
    virtual ~RemoteBuffer() = default;

    // Updates the head position to the new value
    virtual void UpdateHead(uint32_t head);
    
    // Return the rkey of the MR
    virtual uint32_t GetKey();
    // Returns the current tail  
    // There is always at least 4 bytes of usable space directly after the tail
    virtual uint32_t GetTail();
    // Returns the write address if you want to write len bytes to the buffer without updating the tail
    // Can return an error if there is not enough free space
    virtual StatusOr<uint32_t>GetWriteAddr(uint32_t len);
    // Returns the write address if you want to write len bytes to the buffer and updates the tail accordingly
    // Can return an error if there is not enough free space
    virtual StatusOr<uint32_t>Write(uint32_t len);

};

class BasicRingBuffer : public Buffer {
};
class MagicRingBuffer : public Buffer {
};

}
}
