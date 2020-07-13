
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

#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

/*
 * 
 */
class SendReceiveSender : public Sender {
  public:
    SendReceiveSender(std::shared_ptr<endpoint::Endpoint> ep, std::shared_ptr<kym::memory::Allocator> allocator);
    ~SendReceiveSender() = default;

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Free(SendRegion region);
    Status Send(SendRegion region);
  private:
    std::shared_ptr<memory::Allocator> allocator_;
    std::shared_ptr<endpoint::Endpoint> ep_;
};

class SendReceiveReceiver : public Receiver {
  public:
    SendReceiveReceiver(std::shared_ptr<endpoint::Endpoint> ep);
    ~SendReceiveReceiver() = default;

    StatusOr<ReceiveRegion> Receive();
    Status Free(kym::connection::ReceiveRegion);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;
    std::vector<struct ibv_mr *> mrs_; // TODO(Fischi) Should we do this smarter?
};

class SendReceiveConnection : public Connection {
  public:
    SendReceiveConnection(std::unique_ptr<SendReceiveSender> sender, std::unique_ptr<SendReceiveReceiver> receiver);
    ~SendReceiveConnection() = default;

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    std::unique_ptr<SendReceiveSender> sender_;
    std::unique_ptr<SendReceiveReceiver> receiver_;
};

StatusOr<std::unique_ptr<SendReceiveConnection>> DialSendReceive(std::string ip, int port);

class SendReceiveListener {
  public:
    SendReceiveListener(std::unique_ptr<endpoint::Listener> listener);
    ~SendReceiveListener() = default;

    StatusOr<std::unique_ptr<SendReceiveConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
};

StatusOr<std::unique_ptr<SendReceiveListener>> ListenSendReceive(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
