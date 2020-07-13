
/*
 *
 *
 */

#include "endpoint.hpp"

#include <memory> // For smart pointers

#include <infiniband/verbs.h> 

#include "error.hpp"

namespace kym {
namespace endpoint {



ibv_pd Endpoint::GetPd(){
  return ibv_pd{};
}

Status Endpoint::PostSendRaw(struct ibv_send_wr *wr, struct ibv_send_wr **bad_wr){
  return Status(kym::StatusCode::NotImplemented);
}

Status Endpoint::PostSend(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  return Status(kym::StatusCode::NotImplemented);
}
Status Endpoint::PostRead(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  return Status(kym::StatusCode::NotImplemented);
}
Status Endpoint::PostWrite(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  return Status(kym::StatusCode::NotImplemented);
}
StatusOr<ibv_wc> Endpoint::PollSendCq(){
  return Status(kym::StatusCode::NotImplemented);
}

Status Endpoint::PostRecvRaw(struct ibv_recv_wr *wr, struct ibv_recv_wr **bad_wr){
  return Status(kym::StatusCode::NotImplemented);
}

Status Endpoint::PostRecv(uint64_t ctx, uint32_t lkey, void *addr, size_t size){
  return Status(kym::StatusCode::NotImplemented);
}

StatusOr<ibv_wc> Endpoint::PollRecvCq(){
  return Status(kym::StatusCode::NotImplemented);
}

Endpoint::Endpoint(){
}


StatusOr<std::unique_ptr<Endpoint>> Listener::Accept(Options opts){
  return Status(kym::StatusCode::NotImplemented);
}

Listener::Listener() {
}

StatusOr<std::unique_ptr<Endpoint>> Dial(std::string ip, int port, Options opts){
  return Status(kym::StatusCode::NotImplemented);
}

StatusOr<std::unique_ptr<Listener>> Listen(std::string ip, int port){
  return Status(kym::StatusCode::NotImplemented);
}
StatusOr<std::unique_ptr<Listener>> Listen(std::string ip, int port, Options opts){
  return Status(kym::StatusCode::NotImplemented);
}

}
}

