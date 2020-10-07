
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
    Status Send(SendRegion region);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    memory::Allocator *allocator_;
    endpoint::Endpoint *ep_;

    endpoint::IReceiveQueue *rq_;
    bool rq_shared_;

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
  private:
    endpoint::Listener *listener_;
    endpoint::SharedReceiveQueue *srq_;
};

StatusOr<SendReceiveListener *> ListenSendReceive(std::string ip, int port);
StatusOr<SendReceiveListener *> ListenSharedReceive(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
