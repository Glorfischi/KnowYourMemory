
/*
 *
 *  receive_queue.hpp
 *
 *  classes to manage receive buffer management for receive queues and shared receiev queues 
 *
 */

#ifndef KNY_RECEIVE_QUEUE_HPP_
#define KNY_RECEIVE_QUEUE_HPP_

#include <stddef.h>

#include <cstdint>
#include <infiniband/verbs.h> 
#include <vector>
#include <mutex>

#include "endpoint.hpp"
#include "error.hpp"

namespace kym {
namespace endpoint {


/*
 * Common interface
 */
struct mr {
  void		    *addr;
	size_t			length;
};

class IReceiveQueue {
  public:
    virtual ~IReceiveQueue() = default;
    // Cleanup of receive buffers
    virtual Status Close() = 0;

    // Returns the MR corresponding to the work request id of a receive completion event.
    virtual struct mr GetMR(uint32_t wr_id) = 0;
    // Reposts the MR corresponding to the work request id of a receive completion event.
    virtual Status PostMR(uint32_t wr_id) = 0;
};

/*
 * Implementation of the ReceiveQueue Interface for a simple non shared receive queue.
 * Calling this might not be thread safe. TODO(Fischi) It actually probably is?
 */
class ReceiveQueue : public IReceiveQueue {
  public:
    ReceiveQueue(Endpoint *ep, ibv_mr* mr, size_t transfer_size): ep_(ep), mr_(mr), transfer_size_(transfer_size){}; 
    ~ReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);
  private:
    Endpoint *ep_;
    ibv_mr* mr_;
    size_t transfer_size_;
};
StatusOr<ReceiveQueue *> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight);



/*
 * Implementation of the ReceiveQueue Interface for a  shared receive queue.
 */
class SharedReceiveQueue : public IReceiveQueue {
  public:
    SharedReceiveQueue(struct ibv_srq *srq, ibv_mr* mr, size_t transfer_size): 
      srq_(srq), mr_(mr), transfer_size_(transfer_size), max_unposted_(30), unposted_(0){
        this->to_post_.reserve(this->max_unposted_);
      }; 
    ~SharedReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);

    struct ibv_srq *GetSRQ();
  private:
    struct ibv_srq *srq_;
    ibv_mr* mr_;
    size_t transfer_size_;

    int max_unposted_;
    int unposted_;
    std::vector<uint64_t> to_post_;
    std::mutex lock_;
};
StatusOr<SharedReceiveQueue *> GetSharedReceiveQueue(struct ibv_pd *pd, size_t transfer_size, size_t inflight);




}
}

#endif // KNY_RECEIVE_QUEUE_HPP_
