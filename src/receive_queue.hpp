
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

    virtual int FastPostMR(uint32_t *array, int size) = 0;
};

/*
 * Implementation of the ReceiveQueue Interface for a simple non shared receive queue.
 * Calling this might not be thread safe. TODO(Fischi) It actually probably is?
 */
class ReceiveQueue : public IReceiveQueue {
  public:
    ReceiveQueue(Endpoint *ep, ibv_mr* mr, size_t transfer_size): ep_(ep), mr_(mr), transfer_size_(transfer_size){
      for(int i = 0; i<128; i++){
        recv_sges[i].addr = 0;
        recv_sges[i].length = this->transfer_size_;
        recv_sges[i].lkey = this->mr_->lkey;
        recv_wrs[i].wr_id = 0;
        recv_wrs[i].next = &recv_wrs[i+1];
        recv_wrs[i].sg_list = &recv_sges[i];
        recv_wrs[i].num_sge = 1;
      }

    }; 
    ~ReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);

    int FastPostMR(uint32_t *array, int size); // cannot make inline because of "virtual". 


  private:

    inline uint64_t ContextToAddr(uint32_t wr_id){
      return ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
    }

    Endpoint *ep_;
    ibv_mr* mr_;
    size_t transfer_size_;


    struct ibv_sge recv_sges[128];    // TODO. 128 should be defined 
    struct ibv_recv_wr recv_wrs[128];   // TODO. 128 should be defined 
};
StatusOr<ReceiveQueue *> GetReceiveQueue(Endpoint *ep, size_t transfer_size, size_t inflight);



/*
 * Implementation of the ReceiveQueue Interface for a  shared receive queue.
 */
class SharedReceiveQueue : public IReceiveQueue {
  public:
    SharedReceiveQueue(struct ibv_srq *srq, ibv_mr* mr, size_t transfer_size): 
      srq_(srq), mr_(mr), transfer_size_(transfer_size)
    {

      for(int i = 0; i<128; i++){
        recv_sges[i].addr = 0;
        recv_sges[i].length = this->transfer_size_;
        recv_sges[i].lkey = this->mr_->lkey;
        recv_wrs[i].wr_id = 0;
        recv_wrs[i].next = &recv_wrs[i+1];
        recv_wrs[i].sg_list = &recv_sges[i];
        recv_wrs[i].num_sge = 1;
      }

    }; 
    ~SharedReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);

    int FastPostMR(uint32_t *array, int size); // cannot make inline because of "virtual". 
 
    struct ibv_srq *GetSRQ();
  private:

    inline uint64_t ContextToAddr(uint32_t wr_id){
      return ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
    }
    struct ibv_srq *srq_;
    ibv_mr* mr_;
    size_t transfer_size_;

    struct ibv_sge recv_sges[128];    // TODO. 128 should be defined 
    struct ibv_recv_wr recv_wrs[128];   // TODO. 128 should be defined 
};
StatusOr<SharedReceiveQueue *> GetSharedReceiveQueue(struct ibv_pd *pd, size_t transfer_size, size_t inflight);




}
}

#endif // KNY_RECEIVE_QUEUE_HPP_
