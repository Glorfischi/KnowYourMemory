
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_CONN_DIRECT_WRITE_HPP_
#define KNY_CONN_DIRECT_WRITE_HPP_

#include <stddef.h>
#include <string>
#include <vector>


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

struct DirectWriteReceiveBuffer {
  bool valid;
  uint64_t addr;
  uint32_t rkey;
};

class DirectWriteConnection : public Connection {
  public:
    DirectWriteConnection();
    ~DirectWriteConnection();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);

    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Wait(uint64_t id);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

    endpoint::Endpoint *GetEndpoint(){ return this->ep_; };
  private:
    memory::Allocator *allocator_;
    endpoint::Endpoint *ep_;

    uint64_t ackd_id_;
    uint64_t next_id_;

    uint64_t nr_buffers_;
    uint64_t buf_size_;

    // target buffers to write to. Will be updated by remote
    std::vector<DirectWriteReceiveBuffer> target_buf_;

    // Local Rcv buffer
    std::vector<DirectWriteReceiveBuffer> rcv_buffers_;
    uint32_t rcv_head_;
    uint32_t rcv_tail_;

    // Remote target information
    uint64_t r_rcv_buf_addr;
    uint32_t r_rcv_buf_key;
    uint32_t r_rcv_tail_;
};


class DirectWriteListener {
  public:
    DirectWriteListener(endpoint::Listener *listener);
    ~DirectWriteListener();

    Status Close();

    StatusOr<DirectWriteConnection *> Accept();
    endpoint::Listener *GetListener(){return this->listener_;};
  private:
    endpoint::Listener *listener_;
};




}
}

#endif // KNY_CONN_DIRECT_WRITE_HPP_
