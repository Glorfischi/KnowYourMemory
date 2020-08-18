
/*
 *
 *  receive_queue.hpp
 *
 *  classes to manage receive buffer management for receive queues and shared receiev queues 
 *
 */

#ifndef KNY_RECEIVE_QUEUE_HPP_
#define KNY_RECEIVE_QUEUE_HPP_

#include <bits/stdint-uintn.h>
#include <memory>
#include <stddef.h>

#include <infiniband/verbs.h> 
#include <vector>

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
StatusOr<std::unique_ptr<ReceiveQueue>> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight);



/*
 * Implementation of the ReceiveQueue Interface for a  shared receive queue.
 */
class SharedReceiveQueue : public IReceiveQueue {
  public:
    SharedReceiveQueue(struct ibv_srq *srq, std::vector<ibv_mr*> mrs): srq_(srq), mrs_(mrs){}; 
    ~SharedReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);

    struct ibv_srq *GetSRQ();
  private:
    struct ibv_srq *srq_;
    std::vector<ibv_mr*> mrs_;
};
StatusOr<std::shared_ptr<SharedReceiveQueue>> GetSharedReceiveQueue(struct ibv_pd pd, size_t transfer_size, size_t inflight);




}
}

#endif // KNY_RECEIVE_QUEUE_HPP_
