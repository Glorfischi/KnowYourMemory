
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


    // StatusOr<ReceiveRegion> GetReceiveRegion(size_t size);
    StatusOr<ReceiveRegion> Receive();
    // Status Receive(ReceiveRegion reg);
    Status Free(ReceiveRegion);
  private:
    endpoint::Endpoint *ep_;
    memory::Allocator *allocator_;
    endpoint::IReceiveQueue *rq_;
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
