
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_CONN_SHARED_RECEIVE_H_
#define KNY_CONN_SHARED_RECEIVE_H_

#include <stddef.h>
#include <string>
#include <vector>


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "kym/conn.hpp"
#include "kym/mm.hpp"

#include "conn/send_receive.hpp"

namespace kym {
namespace connection {

class SharedReceiver : public Receiver {
  public:
    SharedReceiver(struct rdma_cm_id *id);
    ~SharedReceiver();
    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    struct rdma_cm_id *id_;
    struct ibv_qp *qp_;

    std::vector<struct ibv_mr *> mrs_;
};

class SharedReceiveConnection : public Connection {
  public:
    SharedReceiveConnection(SendReceiveSender *sender, SharedReceiver *receiver);
    ~SharedReceiveConnection();

    kym::memory::Region GetMemoryRegion(size_t size);
    int Send(kym::memory::Region region);
    void Free(kym::memory::Region region);

    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    SendReceiveSender *sender_;
    SharedReceiver *receiver_;
};

int DialSharedReceive(SharedReceiveConnection **, std::string ip, int port);
SharedReceiveConnection *DialSharedReceive(std::string ip, int port);

class SharedReceiveListener {
  public:
    SharedReceiveListener(struct rdma_cm_id *id);
    ~SharedReceiveListener();
    SharedReceiveConnection *Accept();
  private:
    struct rdma_cm_id *ln_id_;

    struct ibv_srq *srq_;
};

int ListenSharedReceive(SharedReceiveListener **,std::string ip, int port);
SharedReceiveListener *ListenSharedReceive(std::string ip, int port);

}
}

#endif // KNY_CONN_SHARED_RECEIVE_H_
