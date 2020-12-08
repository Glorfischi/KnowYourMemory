
/*
 *
 *  shared_receive_queue.hpp
 *
 *  classes to manage receive buffer management shared receiev queues 
 *
 */

#ifndef KNY_SHARED_RECEIVE_QUEUE_HPP_
#define KNY_SHARED_RECEIVE_QUEUE_HPP_

#include <stddef.h>

#include <cstdint>
#include <infiniband/verbs.h> 
#include <vector>

#include "endpoint.hpp"
#include "error.hpp"

#include "readerwriterqueue.h"
#include "atomicops.h"

#include "receive_queue.hpp"

namespace kym {
namespace endpoint {
/*
 * Implementation of the shared receive queue
 * 
 */
class SharedReceiveQueueStub;
class SharedReceiveQueue : public IReceiveQueue {
  public:
    SharedReceiveQueue(struct ibv_srq *srq, ibv_mr* mr, size_t transfer_size, 
        int max_rcv_mr, struct ibv_recv_wr *mr_wr, struct ibv_sge *mr_sge)
      : srq_(srq), mr_(mr), transfer_size_(transfer_size),
      current_rcv_mr_(max_rcv_mr), max_rcv_mr_(max_rcv_mr),recv_mr_wrs_(mr_wr), recv_mr_sge_(mr_sge){}; 
    ~SharedReceiveQueue() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);

    struct ibv_srq *GetSRQ();

    StatusOr<SharedReceiveQueueStub *> NewReceiver();
    friend class SharedReceiveQueueStub;

    Status Run();
  private:
    struct ibv_srq *srq_;

    ibv_mr* mr_;
    size_t transfer_size_;

    inline uint64_t ContextToAddr(uint32_t wr_id){
      return ((uint64_t)this->mr_->addr) + wr_id*this->transfer_size_;
    }
       
    volatile bool running = true;

    int current_rcv_mr_;
    int max_rcv_mr_;
    struct ibv_recv_wr *recv_mr_wrs_;
    struct ibv_sge *recv_mr_sge_;

    // TODO(Fischi) This is not threadsave if we add more receivers after starting to listen.
    std::vector<moodycamel::ReaderWriterQueue<uint32_t> *> queues_;
};

StatusOr<SharedReceiveQueue *> GetSharedReceiveQueue(struct ibv_pd *pd, size_t transfer_size, size_t inflight);

class SharedReceiveQueueStub : public IReceiveQueue {
  public:
    SharedReceiveQueueStub(SharedReceiveQueue *parent, moodycamel::ReaderWriterQueue<uint32_t> *queue)
      : parent_(parent), queue_(queue) {};
    ~SharedReceiveQueueStub() = default;

    Status Close();

    struct mr GetMR(uint32_t wr_id);
    Status PostMR(uint32_t wr_id);
  private:
    SharedReceiveQueue *parent_;
    moodycamel::ReaderWriterQueue<uint32_t> *queue_;
};
}
}

#endif // KNY_SHARED_RECEIVE_QUEUE_HPP_
