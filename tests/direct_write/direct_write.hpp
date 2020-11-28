
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
  uint64_t addr;
  uint32_t rkey;
  bool valid; // valid is by design at the end of the struct
};

class DirectWriteConnection : public Connection {
  public:
    DirectWriteConnection(endpoint::Endpoint *ep, memory::Allocator *alloc,
        uint64_t nr_buffers, uint32_t buf_size,
        std::vector<struct ibv_mr *> rcv_mrs,
        struct ibv_mr *target_buf_mr,
        uint64_t r_rcv_buf_addr,  uint32_t r_rcv_buf_key
        ) : ep_(ep), allocator_(alloc), nr_buffers_(nr_buffers), buf_size_(buf_size),
            target_buf_mr_(target_buf_mr), rcv_buffer_mrs_(rcv_mrs),
            r_rcv_buf_addr_(r_rcv_buf_addr), r_rcv_buf_key_(r_rcv_buf_key)
    {
      this->ackd_id_ = 0;
      this->next_id_ = 1;

      this->target_buf_ = (DirectWriteReceiveBuffer *)target_buf_mr->addr;

      this->rcv_buffers_.reserve(this->nr_buffers_);
      this->rcv_head_ = 1;
      this->rcv_tail_ = 1;

    };
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
    endpoint::Endpoint *ep_;
    memory::Allocator *allocator_;

    uint64_t ackd_id_;
    uint64_t next_id_;

    uint64_t nr_buffers_;
    uint32_t buf_size_;

    // target buffers to write to. Will be updated by remote
    struct ibv_mr *target_buf_mr_;
    DirectWriteReceiveBuffer *target_buf_;

    // Local Rcv buffer
    std::vector<struct ibv_mr *> rcv_buffer_mrs_;
    std::vector<DirectWriteReceiveBuffer> rcv_buffers_;
    uint32_t rcv_head_;
    uint32_t rcv_tail_;

    // Remote target information
    uint64_t r_rcv_buf_addr_;
    uint32_t r_rcv_buf_key_;
};

StatusOr<DirectWriteConnection *> DialDirectWrite(std::string ip, int port, int32_t buf_size);

class DirectWriteListener {
  public:
    DirectWriteListener(endpoint::Listener *listener) : listener_(listener) {};
    ~DirectWriteListener();

    Status Close();

    StatusOr<DirectWriteConnection *> Accept(int32_t buf_size);
    endpoint::Listener *GetListener(){return this->listener_;};
  private:
    endpoint::Listener *listener_;
};


StatusOr<DirectWriteListener *> ListenDirectWrite(std::string ip, int port); 

}
}

#endif // KNY_CONN_DIRECT_WRITE_HPP_
