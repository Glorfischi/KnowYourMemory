#include "write_duplex.hpp"

#include <bits/stdint-uintn.h>
#include <iostream>
#include <ostream>
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
#include "mm/dumb_allocator.hpp"

namespace kym {
namespace connection {

// Max number of outstanding sends
const int write_duplex_outstanding = 10;
const size_t write_duplex_buf_size = 10*1024*1024;

struct connectionInfo {
  uint64_t addr;
  uint32_t key;
};

endpoint::Options default_write_duplex_options = {
  .qp_attr = {
    .cap = {
      .max_send_wr = write_duplex_outstanding,
      .max_recv_wr = write_duplex_outstanding,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = sizeof(connectionInfo),
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 0,
  .initiator_depth =  write_duplex_outstanding,
  .retry_count = 8,  
  .rnr_retry_count = 7, 
};


StatusOr<std::unique_ptr<WriteDuplexConnection>> DialWriteDuplex(std::string ip, int port){
  return Status(StatusCode::NotImplemented);
}

StatusOr<std::unique_ptr<WriteDuplexListener>> ListenWriteDuplex(std::string ip, int port){
  return Status(StatusCode::NotImplemented);
}


WriteDuplexListener::WriteDuplexListener(std::unique_ptr<endpoint::Listener> listener){
}
Status WriteDuplexListener::Close(){
  return Status(StatusCode::NotImplemented);
}
StatusOr<std::unique_ptr<WriteDuplexConnection>> WriteDuplexListener::Accept(){
  return Status(StatusCode::NotImplemented);
}


WriteDuplexConnection::WriteDuplexConnection(std::shared_ptr<endpoint::Endpoint> ep, struct ibv_mr *buf_mr, 
    std::shared_ptr<kym::endpoint::IReceiveQueue> rq, std::shared_ptr<memory::Allocator>, 
    uint64_t rbuf_vaddr_, uint32_t rbuf_rkey_){
}
Status WriteDuplexConnection::Close(){
  return Status(StatusCode::NotImplemented);
}
StatusOr<ReceiveRegion> WriteDuplexConnection::Receive(){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Free(ReceiveRegion){
  return Status(StatusCode::NotImplemented);
}
StatusOr<SendRegion> WriteDuplexConnection::GetMemoryRegion(size_t size){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Send(SendRegion region){
  return Status(StatusCode::NotImplemented);
}
Status WriteDuplexConnection::Free(SendRegion region){
  return Status(StatusCode::NotImplemented);
}


}
}
