
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

class SendReceiveConnection : public Connection {
  public:
    SendReceiveConnection(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<endpoint::IReceiveQueue>, 
        bool rq_shared, std::shared_ptr<memory::Allocator>);
    ~SendReceiveConnection() = default;

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(std::vector<SendRegion> regions);
    Status Send(SendRegion region);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    std::shared_ptr<memory::Allocator> allocator_;
    std::shared_ptr<endpoint::Endpoint> ep_;

    std::shared_ptr<endpoint::IReceiveQueue> rq_;
    bool rq_shared_;

};

StatusOr<std::unique_ptr<SendReceiveConnection>> DialSendReceive(std::string ip, int port);
StatusOr<std::unique_ptr<SendReceiveConnection>> DialSendReceive(std::string ip, int port, std::shared_ptr<endpoint::IReceiveQueue> rq);

class SendReceiveListener {
  public:
    SendReceiveListener(std::unique_ptr<endpoint::Listener> listener);
    SendReceiveListener(std::unique_ptr<endpoint::Listener> listener, std::shared_ptr<endpoint::SharedReceiveQueue> srq);
    ~SendReceiveListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<SendReceiveConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
    std::shared_ptr<endpoint::SharedReceiveQueue> srq_;
};

StatusOr<std::unique_ptr<SendReceiveListener>> ListenSendReceive(std::string ip, int port);
StatusOr<std::unique_ptr<SendReceiveListener>> ListenSharedReceive(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
