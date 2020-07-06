
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


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "kym/conn.hpp"
#include "kym/mm.hpp"

namespace kym {
namespace connection {

/*
 * 
 */
class SendReceiveSender : public Sender {
  public:
    SendReceiveSender(struct rdma_cm_id *id, kym::memory::Allocator *allocator);
    ~SendReceiveSender();
    kym::memory::Region GetMemoryRegion(size_t size);
    void Free(kym::memory::Region region);
    int Send(kym::memory::Region region);
  private:
    struct rdma_cm_id *id_;
    struct ibv_qp *qp_;
    kym::memory::Allocator *allocator_;
};

class SendReceiveReceiver : public Receiver {
  public:
    SendReceiveReceiver(struct rdma_cm_id *id);
    ~SendReceiveReceiver();
    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    struct rdma_cm_id *id_;
    struct ibv_qp *qp_;

    std::vector<struct ibv_mr *> mrs_;

    // ToDo: Some kind of struct to manage MRs
};

class SendReceiveConnection : public Connection {
  public:
    SendReceiveConnection(SendReceiveSender *sender, SendReceiveReceiver *receiver);
    ~SendReceiveConnection();

    kym::memory::Region GetMemoryRegion(size_t size);
    int Send(kym::memory::Region region);
    void Free(kym::memory::Region region);

    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    SendReceiveSender *sender_;
    SendReceiveReceiver *receiver_;
};

int DialSendReceive(SendReceiveConnection **, std::string ip, int port);
SendReceiveConnection *DialSendReceive(std::string ip, int port);

class SendReceiveListener {
  public:
    SendReceiveListener(struct rdma_cm_id *id);
    ~SendReceiveListener();
    SendReceiveConnection *Accept();
  private:
    struct rdma_cm_id *ln_id_;
};

int ListenSendReceive(SendReceiveListener **,std::string ip, int port);
SendReceiveListener *ListenSendReceive(std::string ip, int port);



}
}

#endif // KNY_CONN_SEND_RECEIVE_H_
