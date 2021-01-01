
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
#include <map>


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
    ReadConnection(endpoint::Endpoint *ep, memory::Allocator *allocator, endpoint::IReceiveQueue *rq, bool fence);
    ReadConnection(endpoint::Endpoint *ep, memory::Allocator *allocator, endpoint::IReceiveQueue *rq)
      : ReadConnection(ep, allocator, rq, false){};
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


    // Receives into provided memory region. Will return an id to WaitReceive on. If provided it will return the
    // number of bytes written to length
    StatusOr<uint64_t> ReceiveAsync(ReceiveRegion reg);
    StatusOr<uint64_t> ReceiveAsync(ReceiveRegion reg, uint32_t *length);
    Status WaitReceive(uint64_t id);

  private:
    endpoint::Endpoint *ep_;
    memory::Allocator *allocator_;
    endpoint::IReceiveQueue *rq_;

    std::vector<struct ibv_send_wr> outstanding_read_wr_;
    std::vector<struct ibv_sge> outstanding_read_sge_;
    bool fence_;
    uint64_t ackd_rcv_id_;
    uint64_t next_rcv_id_;

    StatusOr<ReadRequest> ReceiveRequest();
    StatusOr<uint64_t> ReadInto(void *addr, uint32_t key, ReadRequest req);
};

StatusOr<ReadConnection *> DialRead(std::string ip, int port, bool fence);
StatusOr<ReadConnection *> DialRead(std::string ip, int port);

class ReadListener {
  public:
    ReadListener(endpoint::Listener *ln) : listener_(ln){};
    ~ReadListener();

    Status Close();

    StatusOr<ReadConnection*> Accept(bool fence);
    StatusOr<ReadConnection*> Accept();
  private:
    endpoint::Listener *listener_;
};

StatusOr<ReadListener *> ListenRead(std::string ip, int port);

}
}

#endif // KNY_CONN_READ_H_
