
/*
 *
 *
 */

#ifndef KNY_CONN_BUFFERED_READ_HPP_
#define KNY_CONN_BUFFERED_READ_HPP_

#include <cstdint>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory>
#include <list>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "conn.hpp"
#include "mm.hpp"
#include "endpoint.hpp"
#include "receive_queue.hpp"

#include "debug.h"

namespace kym {
namespace connection {

class BufferedReadConnection : public Connection {
  public:
    BufferedReadConnection(endpoint::Endpoint *ep,
        struct ibv_mr *meta_data, struct ibv_mr *buffer, uint32_t buf_len,
        struct ibv_mr *remote_meta_data, uint32_t remote_meta_rkey, uint64_t remote_head_addr, uint64_t remote_tail_addr,
        struct ibv_mr *remote_buffer, uint32_t remote_buffer_rkey, uint64_t remote_buffer_addr, uint32_t remote_buffer_len
        ) : ep_(ep),
            meta_data_(meta_data), head_((uint64_t *)meta_data->addr), tail_((uint64_t *)meta_data->addr + 1), 
            buffer_(buffer), length_(buf_len),
            remote_meta_data_(remote_meta_data), remote_meta_rkey_(remote_meta_rkey), 
            remote_head_((uint64_t *)remote_meta_data->addr), read_ptr_(0), remote_head_addr_(remote_head_addr),
            remote_tail_((volatile uint64_t *) remote_meta_data->addr + 1), remote_tail_addr_(remote_tail_addr),
            remote_buffer_(remote_buffer), remote_buf_len_(remote_buffer_len), 
            remote_buf_rkey_(remote_buffer_rkey) ,remote_buf_addr_(remote_buffer_addr)
    {
      *this->head_ = 0;
      *this->tail_ = 0;
      this->acked_head_ =0;
      debug(stderr, "New connection [head: %p, tail: %p]\n", this->head_, this->tail_);
    };
    ~BufferedReadConnection();

    Status Close();


    // Debug function to get the mean transfer size for the receiver.
    uint64_t GetMeanMsgSize();

    // We will need to copy into anyway
    Status Send(void *buf, uint32_t length);

    // These are just to satisfy the connection requirements so that we can use the standard benchmark functions
    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Free(SendRegion region);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Wait(uint64_t id);

    StatusOr<ReceiveRegion> Receive(); 
    // (Fischi) An Async receive would probably result in an out of order receive. This way we will just periodically "pay" for the transfer
    Status Free(ReceiveRegion);
  private:
    endpoint::Endpoint *ep_;

    // Sender
    struct ibv_mr *meta_data_;
    volatile uint64_t *head_;
    volatile uint64_t *tail_; // (Fischi) volatile too?
    struct ibv_mr *buffer_;
    uint32_t length_;

    // Receiver - Buffer copy
    struct ibv_mr *remote_meta_data_;
    uint32_t remote_meta_rkey_;
    uint64_t *remote_head_; // (Fischi) volatile too?
    uint64_t read_ptr_;
    uint64_t acked_head_;
    uint64_t remote_head_addr_;
    volatile uint64_t *remote_tail_;
    uint64_t remote_tail_addr_;
    struct ibv_mr *remote_buffer_;
    uint32_t remote_buf_len_;
    uint32_t remote_buf_rkey_;
    uint64_t remote_buf_addr_;

    // Debug
    uint64_t d_sent_msgs_ = 0;
    uint64_t d_sent_bytes_ = 0;

    std::list<uint64_t> outstanding_;
};


StatusOr<BufferedReadConnection *> DialBufferedRead(std::string ip, int port);

class BufferedReadListener {
  public:
    BufferedReadListener(endpoint::Listener *ln) : listener_(ln){};
    ~BufferedReadListener();

    Status Close();

    StatusOr<BufferedReadConnection*> Accept();
  private:
    endpoint::Listener *listener_;
};

StatusOr<BufferedReadListener *> ListenBufferedRead(std::string ip, int port);

}
}

#endif // KNY_CONN_BUFFERED_READ_HPP_
