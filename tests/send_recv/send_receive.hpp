
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_CONN_SEND_RECEIVE_H_
#define KNY_CONN_SEND_RECEIVE_H_

#include <stddef.h>
#include <string>
#include <vector>
#include <memory> // For smart pointers


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

class SendReceiveConnection : public Connection, public BatchSender {
  public:
    SendReceiveConnection(endpoint::Endpoint *, endpoint::IReceiveQueue *, 
        bool rq_shared, memory::Allocator *);
    ~SendReceiveConnection();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(std::vector<SendRegion> regions);
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Free(SendRegion region);
    Status Wait(uint64_t id);

    StatusOr<ReceiveRegion> Receive();

    Status Free(ReceiveRegion);


    ReceiveRegion FastReceive(){ // KOnstantin BUt to be honest, I think "int FastReceive(ReceiveRegion&)" is more general
      struct ibv_wc *wc = this->ep_->FastPollRecvCq();
      assert(wc!=NULL && "null completion");
      uint64_t context = wc->wr_id; // copy as it can become invalid after next PollRecvCq
      uint32_t size = wc->byte_len; // copy as it can become invalid after next PollRecvCq

      ReceiveRegion reg = {context, this->rq_->GetMR(context).addr , size, 0};
      return reg;
    }

    int FastFree(ReceiveRegion& region){
        recv_contexts[recv_batch_counter++] = region.context; 

        if(recv_batch_counter == batch_size){
            recv_batch_counter = 0;
            return this->rq_->FastPostMR(recv_contexts,batch_size);
        }
        return 0;
    }


    endpoint::Endpoint *GetEndpoint(){ return this->ep_; };
  private:
    memory::Allocator *allocator_;
    endpoint::Endpoint *ep_;

    uint64_t ackd_id_;
    uint64_t next_id_;

    endpoint::IReceiveQueue *rq_;
    bool rq_shared_;


    static const int batch_size = 32;
    int recv_batch_counter = 0;
    uint32_t recv_contexts[batch_size]; 

};

StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port);
StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, std::string src);
StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, endpoint::IReceiveQueue *rq);
StatusOr<SendReceiveConnection *> DialSendReceive(std::string ip, int port, std::string src, endpoint::IReceiveQueue *rq);

class SendReceiveListener {
  public:
    SendReceiveListener(endpoint::Listener *listener);
    SendReceiveListener(endpoint::Listener *listener, endpoint::SharedReceiveQueue *srq);
    ~SendReceiveListener();

    Status Close();

    StatusOr<SendReceiveConnection *> Accept();
    endpoint::Listener *GetListener(){return this->listener_;};
  private:
    endpoint::Listener *listener_;
    endpoint::SharedReceiveQueue *srq_;
};

StatusOr<SendReceiveListener *> ListenSendReceive(std::string ip, int port);
StatusOr<SendReceiveListener *> ListenSharedReceive(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
