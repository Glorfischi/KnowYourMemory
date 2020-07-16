
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_CONN_SHARED_RECEIVE_H_
#define KNY_CONN_SHARED_RECEIVE_H_

#include <bits/stdint-uintn.h>
#include <memory>
#include <stddef.h>
#include <string>
#include <vector>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "error.hpp"
#include "endpoint.hpp"
#include "conn.hpp"
#include "mm.hpp"

namespace kym {
namespace connection {

class SharedReceiveQueue {
  public:
    friend class SharedReceiveListener;

    SharedReceiveQueue(struct ibv_pd pd);
    ~SharedReceiveQueue() = default;

    Status Close();

    Status PostReceiveRegion(ReceiveRegion reg);
    StatusOr<ReceiveRegion> GetRegionById(uint64_t id);

  private:
    struct ibv_srq *srq_;
    std::vector<struct ibv_mr *> mrs_;
};

class SharedReceiveConnection : public Connection {
  public:
    SharedReceiveConnection(std::shared_ptr<endpoint::Endpoint>, std::shared_ptr<SharedReceiveQueue>);
    ~SharedReceiveConnection() = default;

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Free(SendRegion region);

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);

  private:
    std::shared_ptr<endpoint::Endpoint> ep_;
    std::shared_ptr<memory::Allocator> allocator_;
    std::shared_ptr<SharedReceiveQueue> srq_;

};

StatusOr<std::unique_ptr<SharedReceiveConnection>> DialSharedReceive(std::string ip, int port);

class SharedReceiveListener {
  public:
    SharedReceiveListener(std::unique_ptr<endpoint::Listener> ln, std::shared_ptr<SharedReceiveQueue> srq);
    ~SharedReceiveListener() = default;

    Status Close();

    StatusOr<std::unique_ptr<SharedReceiveConnection>> Accept();
  private:
    std::unique_ptr<endpoint::Listener> listener_;
    std::shared_ptr<SharedReceiveQueue> srq_;

};

StatusOr<std::unique_ptr<SharedReceiveListener>> ListenSharedReceive(std::string ip, int port);

}
}

#endif // KNY_CONN_SHARED_RECEIVE_H_
