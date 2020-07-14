
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_CONN_READ_H_
#define KNY_CONN_READ_H_

#include <bits/stdint-uintn.h>
#include <stddef.h>
#include <string>
#include <vector>
#include <memory>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "conn.hpp"
#include "mm.hpp"
#include "endpoint.hpp"

namespace kym {
namespace connection {


struct ReadRequest {
  uint64_t    addr;
  uint32_t    key;
	uint32_t		length;
};

/*
 * 
 */
class ReadSender : public Sender {
  public:
    ReadSender(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<memory::Allocator>, memory::Region ack);
    ~ReadSender();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Free(SendRegion region);
    Status Send(SendRegion region);
  private:
    std::shared_ptr<memory::Allocator> allocator_;
    std::shared_ptr<endpoint::Endpoint> ep_;

    memory::Region ack_; // Region the receiver write to to acknowlege that the read was successfull
};

class ReadReceiver : public Receiver {
  public:
    ReadReceiver(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<memory::Allocator>, 
        memory::Region ack, uint64_t ack_remote_addr, uint32_t ack_remote_key);
    ~ReadReceiver();

    StatusOr<ReceiveRegion> Receive();
    Status Free(kym::connection::ReceiveRegion);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;
    std::shared_ptr<memory::Allocator> allocator_;

    std::vector<struct ibv_mr *> mrs_;

    memory::Region ack_; // Region to send ack from
    uint64_t ack_remote_addr_;
    uint32_t ack_remote_key_;
};

class ReadConnection : public Connection {
  public:
    ReadConnection(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<memory::Allocator> allocator,
        memory::Region ack, memory::Region ack_source, uint64_t ack_remote_addr, uint32_t ack_remote_key);
    ~ReadConnection() = default;

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    std::shared_ptr<endpoint::Endpoint> ep_;

    std::unique_ptr<ReadSender> sender_;
    std::unique_ptr<ReadReceiver> receiver_;
};

StatusOr<std::unique_ptr<ReadConnection>> DialRead(std::string ip, int port);

class ReadListener {
  public:
    ReadListener(std::unique_ptr<endpoint::Listener>);
    ~ReadListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<ReadConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
};

StatusOr<std::unique_ptr<ReadListener>> ListenRead(std::string ip, int port);

}
}

#endif // KNY_CONN_READ_H_
