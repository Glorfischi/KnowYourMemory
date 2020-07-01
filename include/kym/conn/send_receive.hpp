
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_KNY_CONN_SEND_RECEIVE_H_
#define KNY_KNY_CONN_SEND_RECEIVE_H_

#include <stddef.h>

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
    SendReceiveSender();
    kym::memory::Region GetMemoryRegion(size_t size);
    int Send(kym::memory::Region region);
  private:
    struct ibv_qp *qp_;

    kym::memory::Allocator allocator;
};

class SendReceiveReceiver : public Receiver {
  public:
    kym::memory::Region Receive();
    void Free(kym::memory::Region region);
  private:
    struct ibv_qp *qp_;

    // ToDo: Some kind of struct to manage MRs
};

class SendReceiveConnection : public Connection {
  public:
    kym::memory::Region AllocateMR(size_t size);
    int Send(kym::memory::Region region);

    kym::memory::Region Receive();
    void Free(kym::memory::Region region);
};

int DialSendReceive(SendReceiveConnection &);
SendReceiveConnection DialSendReceive(){
  SendReceiveConnection conn;
  DialSendReceive(conn);
  return conn;
};


}
}

#endif // KNY_KNY_CONN_SEND_RECEIVE_H_
