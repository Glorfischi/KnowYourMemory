
#ifndef KNY_WRITE_ATOMIC_HPP
#define KNY_WRITE_ATOMIC_HPP

#include <cstdint>
#include <stddef.h>
#include <string>
#include <vector>
#include <map>

#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"
#include "shared_receive_queue.hpp"

#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

// Changing order here will break the code
struct write_atomic_meta {
  uint64_t head;
  uint64_t tail;
};

struct write_atomic_inst_opts {
  uint32_t buf_size;
  bool use_dm;
  bool use_srq;
};
namespace {
  const write_atomic_inst_opts default_write_atomic_inst_opts = {
    .buf_size=10*1024*1024,
    .use_dm = true,
    .use_srq = false,
  };
}

struct write_atomic_conn_opts {
  bool standalone_receive; // Whether the resulting connection handles receiving. O/w the instance will handle incomming messages
};
namespace {
  const write_atomic_conn_opts default_write_atomic_conn_opts = {
    .standalone_receive  = false,
  };
}

class WriteAtomicConnection;

// An Instance contains the shared Ringbuffer and allows you to
// dial and accept new connections
class WriteAtomicInstance : public Receiver {
  public:
    // Memory regions will be lazily initialized on the first Listen() or Dial()
    WriteAtomicInstance(write_atomic_inst_opts opts): pd_(nullptr), listener_(nullptr), use_srq_(opts.use_srq), srq_(nullptr),
       buf_mr_(nullptr), buf_size_(opts.buf_size), use_dm_(opts.use_dm), buf_meta_mr_(nullptr), buf_meta_(nullptr)  {};
    WriteAtomicInstance(): WriteAtomicInstance(default_write_atomic_inst_opts) {};
    ~WriteAtomicInstance();

    // Listen for new connections on the provided ip and port
    Status Listen(std::string ip, int port);
    Status Close();

    StatusOr<WriteAtomicConnection *> Accept() { 
      return this->Accept(default_write_atomic_conn_opts);
    };
    StatusOr<WriteAtomicConnection *> Accept(write_atomic_conn_opts opts);
    StatusOr<WriteAtomicConnection *> Dial(std::string ip, int port) {
      return this->Dial(ip, port, default_write_atomic_conn_opts);
    }
    StatusOr<WriteAtomicConnection *> Dial(std::string ip, int port, write_atomic_conn_opts opts);

    // Receiving on not delegated receive cq's
    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

  private:
    Status Init(struct ibv_context *ctx, struct ibv_pd *pd);

    struct ibv_context *ctx_;
    struct ibv_pd *pd_;
    struct ibv_cq *rcv_cq_;
    endpoint::Listener *listener_;
    std::vector<WriteAtomicConnection*> conns_;

    bool                                        use_srq_;
    endpoint::SharedReceiveQueue               *srq_;
    std::map<uint32_t, endpoint::ReceiveQueue*> rqs_;

    // Ringbuffer
    struct ibv_mr     *buf_mr_;
    uint32_t           buf_size_;
    std::map<uint64_t, uint64_t> freed_regions_;
    uint64_t           acked_head_ = 0;

    bool               use_dm_;
    struct ibv_dm     *buf_meta_dm_;
    struct ibv_mr     *buf_meta_mr_;   
    write_atomic_meta *buf_meta_;   
};

class WriteAtomicConnection : public Connection {
  public:
    WriteAtomicConnection(endpoint::Endpoint *ep, memory::Allocator *alloc, 
        uint64_t rbuf_vaddr, uint32_t rbuf_rkey, uint32_t rbuf_size,
        uint32_t rbuf_meta_vaddr, uint32_t rbuf_meta_rkey, struct ibv_mr *rbuf_meta_mr)
      : ep_(ep), allocator_(alloc),
        rbuf_vaddr_(rbuf_vaddr), rbuf_rkey_(rbuf_rkey), rbuf_size_(rbuf_size),
        rbuf_meta_vaddr_(rbuf_meta_vaddr), rbuf_meta_rkey_(rbuf_meta_rkey), rbuf_meta_mr_(rbuf_meta_mr),
        rbuf_meta_((write_atomic_meta *)rbuf_meta_mr->addr) {};
    ~WriteAtomicConnection();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Free(SendRegion region);
    Status Send(SendRegion region);

    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Wait(uint64_t id);


    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    endpoint::Endpoint *ep_;
    memory::Allocator *allocator_;

    uint64_t ackd_id_ = 0;
    uint64_t next_id_ = 1;
   
    // Remote Buffer
    uint64_t rbuf_vaddr_;
    uint32_t rbuf_rkey_;
    uint32_t rbuf_size_;

    // Remote Buffer Meta
    uint64_t           rbuf_meta_vaddr_;
    uint32_t           rbuf_meta_rkey_;
    struct ibv_mr     *rbuf_meta_mr_; // Buffer to read into
    write_atomic_meta *rbuf_meta_;   
};

}
}

#endif // KNY_WRITE_ATOMIC_HPP
