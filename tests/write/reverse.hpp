
#ifndef KNY_WRITE_REVERSE_HPP_
#define KNY_WRITE_REVERSE_HPP_

#include <cstdint>
#include <stddef.h>
#include <string>
#include <vector>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"
#include "conn.hpp"
#include "mm.hpp"

#include "acknowledge.hpp"
#include "receive_queue.hpp"
#include "ring_buffer/ring_buffer.hpp"

#include "write.hpp"

namespace kym {
namespace connection {

class WriteReverseReceiver : public WriteReceiver {
  public:
    WriteReverseReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack)
      : WriteReceiver(ep, rbuf, ack) {};
    WriteReverseReceiver(endpoint::Endpoint *ep, bool owns_ep, ringbuffer::Buffer *rbuf, Acknowledger *ack)
      : WriteReceiver(ep, owns_ep, rbuf, ack) {};
    Status Close();
    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
};

class WriteReverseSender : public WriteSender {
  public:
    WriteReverseSender(endpoint::Endpoint *ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : WriteSender(ep, alloc, rbuf, ack) {};
    WriteReverseSender(endpoint::Endpoint *ep, bool owns_ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : WriteSender(ep, owns_ep, alloc, rbuf, ack) {};


    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions);
    Status Wait(uint64_t id);
    Status Free(SendRegion region);
};

}
}

#endif // KNY_WRITE_REVERSE_HPP_
