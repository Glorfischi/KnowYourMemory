
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


#include <rdma/rdma_cma.h>
#include <rdma/rdma_verbs.h>
#include <infiniband/verbs.h>

#include "kym/conn.hpp"
#include "kym/mm.hpp"

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
    ReadSender(struct rdma_cm_id *id, kym::memory::Allocator *allocator);
    ~ReadSender();
    kym::memory::Region GetMemoryRegion(size_t size);
    void Free(kym::memory::Region region);
    int Send(kym::memory::Region region);
  private:
    struct rdma_cm_id *id_;
    struct ibv_qp *qp_;
    kym::memory::Allocator *allocator_;
};

class ReadReceiver : public Receiver {
  public:
    ReadReceiver(struct rdma_cm_id *id, kym::memory::Allocator *allocator);
    ~ReadReceiver();
    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    struct rdma_cm_id *id_;
    struct ibv_qp *qp_;

    std::vector<struct ibv_mr *> mrs_;

    kym::memory::Allocator *allocator_;
    std::vector<kym::memory::Region> recv_regions_;
};

class ReadConnection : public Connection {
  public:
    ReadConnection(ReadSender *sender, ReadReceiver *receiver);
    ~ReadConnection();

    kym::memory::Region GetMemoryRegion(size_t size);
    int Send(kym::memory::Region region);
    void Free(kym::memory::Region region);

    kym::connection::ReceiveRegion Receive();
    void Free(kym::connection::ReceiveRegion);
  private:
    ReadSender *sender_;
    ReadReceiver *receiver_;
};

int DialRead(ReadConnection **, std::string ip, int port);
ReadConnection *DialRead(std::string ip, int port);

class ReadListener {
  public:
    ReadListener(struct rdma_cm_id *id);
    ~ReadListener();
    ReadConnection *Accept();
  private:
    struct rdma_cm_id *ln_id_;
};

int ListenRead(ReadListener **,std::string ip, int port);
ReadListener *ListenRead(std::string ip, int port);

}
}

#endif // KNY_CONN_READ_H_
