
/*
 *
 *
 */

#ifndef KNY_CONN_READ_H_
#define KNY_CONN_READ_H_

#include <cstdint>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "conn.hpp"
#include "mm.hpp"
#include "endpoint.hpp"
#include "receive_queue.hpp"

namespace kym {
namespace connection {


struct ReadRequest {
  uint64_t    addr;
  uint32_t    key;
	uint32_t		length;
};

class ReadConnection : public Connection {
  public:
    ReadConnection(endpoint::Endpoint *ep, memory::Allocator *allocator, endpoint::IReceiveQueue *rq)
      : ep_(ep), allocator_(allocator), rq_(rq){};
    ~ReadConnection();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);

    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);

    Status Wait(uint64_t id);
    Status Free(SendRegion region);


    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

    StatusOr<ReceiveRegion> RegisterReceiveRegion(void *addr, uint32_t length);
    Status DeregisterReceiveRegion(ReceiveRegion reg);
    // Receive directly into provided memory region. The provided region must be a (or be contained in )a region
    // registered by RegisterReceiveRegion. It will return the number of bytes written.
    StatusOr<uint32_t> Receive(ReceiveRegion reg);

  private:
    endpoint::Endpoint *ep_;
    memory::Allocator *allocator_;
    endpoint::IReceiveQueue *rq_;


    StatusOr<ReadRequest> ReceiveRequest();
    Status ReadInto(void *addr, uint32_t key, ReadRequest req);
};

StatusOr<ReadConnection *> DialRead(std::string ip, int port);

class ReadListener {
  public:
    ReadListener(endpoint::Listener *ln) : listener_(ln){};
    ~ReadListener();

    Status Close();

    StatusOr<ReadConnection*> Accept();
  private:
    endpoint::Listener *listener_;
};

StatusOr<ReadListener *> ListenRead(std::string ip, int port);

}
}

#endif // KNY_CONN_READ_H_
