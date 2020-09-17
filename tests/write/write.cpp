#include "write.hpp"

#include "acknowledge.hpp"
#include "conn.hpp"
#include "endpoint.hpp"
#include "error.hpp"
#include "mm.hpp"

#include "mm/dumb_allocator.hpp"


namespace kym {
namespace connection {

namespace {
  struct conn_details {
    WriteOpts opts; // 2 Bytes
    ringbuffer::BufferContext buffer_ctx; // 16 Bytes
    AcknowledgerContext ack_ctx; // 16 bytes
  };

  endpoint::Options write_connection_opts{
  .qp_attr = {
    .cap = {
      .max_send_wr = 10,
      .max_recv_wr = 10,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 0,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 30,
  .initiator_depth =  30,
  .retry_count = 5,  
  .rnr_retry_count = 2, 
  };
}


StatusOr<ReceiveRegion> WriteReceiver::Receive(){
  ReceiveRegion reg;
  void *head = this->rbuf_->GetReadPtr();
  while ( *(volatile uint32_t *)head != 0){}
  uint32_t length = *(uint32_t *)head;
  void *addr = this->rbuf_->Read(length);
  reg.addr = addr;
  reg.length = length;
  reg.context = 0;
  return reg;
}

Status WriteReceiver::Free(ReceiveRegion reg){
  uint32_t head = this->rbuf_->Free(reg.addr);
  this->ack_->Ack(head);
  return Status(StatusCode::NotImplemented);
}


Status WriteReceiver::Close(){
  auto stat = this->rbuf_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing ringbuffer");
  }
  stat = this->ack_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing acknowledger");
  }
  return this->ep_->Close();
}

WriteReceiver::~WriteReceiver(){
  delete this->ack_;
  delete this->rbuf_;
  delete this->ep_;
}

StatusOr<SendRegion> WriteSender::GetMemoryRegion(size_t size){
  SendRegion reg;
  auto buf_s = this->alloc_->Alloc(size + sizeof(uint32_t)); // Add 2 bytes for legth of buffer
  if (!buf_s.ok()){
    return buf_s.status();
  }
  auto buf = buf_s.value();
  reg.addr = (void *)((uint64_t)buf.addr + sizeof(uint32_t));
  reg.length = size;
  reg.lkey = buf.lkey;
  reg.context = buf.context;
  return reg;
}

Status WriteSender::Send(SendRegion reg){
  auto addr_s = this->rbuf_->GetWriteAddr(reg.length);
  if (!addr_s.ok()){
    auto head_s = this->ack_->Get();
    if (!head_s.ok()){
      return addr_s.status().Wrap(head_s.status().message());
    }
    this->rbuf_->UpdateHead(head_s.value());
    addr_s = this->rbuf_->GetWriteAddr(reg.length);
    if (!addr_s.ok()){
      return addr_s.status();
    }
  }
  uint32_t addr = addr_s.value();

  auto stat = this->ep_->PostWrite(reg.context, reg.lkey,(void *)((size_t)reg.addr - sizeof(uint32_t)), 
      reg.length + sizeof(uint32_t), addr, this->rbuf_->GetKey());
  if (!stat.ok()){
    return stat;
  }
  auto wc_s = this->ep_->PollRecvCq();
  if (!wc_s.ok()){
    return wc_s.status();
  }
  this->rbuf_->Write(reg.length);
  return Status();
}

Status WriteSender::Send(std::vector<SendRegion> regions){
  return Status(StatusCode::NotImplemented);
}

Status WriteSender::Free(SendRegion region){
  memory::Region mr = memory::Region();
  mr.addr = region.addr;
  mr.length = region.length;
  mr.lkey = region.lkey;
  mr.context = region.context;
  return this->alloc_->Free(mr);
}

Status WriteSender::Close(){
  auto stat = this->ack_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing acknowledger");
  }
  return this->ep_->Close();
}

WriteSender::~WriteSender(){
  delete this->ack_;
  delete this->rbuf_;
  delete this->ep_;
}

Status WriteConnection::Close(){
  auto stat = this->rcv_->Close();
  if (!stat.ok()){
    return stat.Wrap("Error closing receiver");
  }
  return this->snd_->Close();
}
WriteConnection::~WriteConnection(){
  delete this->rcv_;
  delete this->snd_;
}


/*
 *
 *  CONNECTION SETUP
 *
 */
Status initReceiver(struct ibv_pd *pd, WriteOpts opts, ringbuffer::Buffer **rbuf, Acknowledger **ack){
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      return Status(StatusCode::NotImplemented, "read acknowledger not implemented");
      break;
    case kAcknowledgerSend:
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  switch (opts.buffer) {
    case kBufferBasic:
      return Status(StatusCode::NotImplemented, "basic buffer not implemented");
      break;
    case kBufferMagic:
      return Status(StatusCode::NotImplemented, "magic buffer not implemented");
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown buffer option");
  }
  return Status();
}

Status connectReceiver(endpoint::Endpoint * ep, conn_details det, ringbuffer::Buffer **rbuf, Acknowledger **ack){
  auto opts = det.opts;
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      return Status(StatusCode::NotImplemented, "read acknowledger not implemented");
      break;
    case kAcknowledgerSend:
      {
        auto sa_s = GetSendAcknowledger(ep);
        if (!sa_s.ok()){
          return sa_s.status().Wrap("error setting up send acknowledger");
        }
        *ack = sa_s.value();
      }
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  return Status();
}




Status initSender(ibv_pd *pd, WriteOpts opts, memory::Allocator **alloc){
  switch (opts.allocator) {
    case kAllocatorDumb:
      *alloc = new memory::DumbAllocator(pd);
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown allocator option");
  }
  return Status();
}

Status connectSender(endpoint::Endpoint * ep, conn_details det, ringbuffer::RemoteBuffer **rbuf, AckReceiver **ack){
  auto opts = det.opts;
  switch (opts.acknowledger) {
    case kAcknowledgerRead:
      return Status(StatusCode::NotImplemented, "read acknowledger not implemented");
      break;
    case kAcknowledgerSend:
      {
        auto sa_s = GetSendAckReceiver(ep);
        if (!sa_s.ok()){
          return sa_s.status().Wrap("error setting up send acknowledger");
        }
        *ack = sa_s.value();
      }
      return Status(StatusCode::NotImplemented, "send acknowledger not implemented");
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown acknowledger option");
  }
  switch (opts.buffer) {
    case kBufferBasic:
      return Status(StatusCode::NotImplemented, "basic buffer not implemented");
      break;
    case kBufferMagic:
      return Status(StatusCode::NotImplemented, "magic buffer not implemented");
      break;
    default:
      return Status(StatusCode::InvalidArgument, "unkown buffer option");
  }
  return Status();
}

/*
 *
 * LISTENER
 *
 */

StatusOr<WriteListener*> ListenWrite(std::string ip, int port){
  // Default to port 18515
  if (port == 0) {
    port = 18515;
  }
  auto ln_stat = endpoint::Listen(ip, port);
  if (!ln_stat.ok()){
    return ln_stat.status();
  }
  endpoint::Listener *ln = ln_stat.value();
  return new WriteListener(ln);
}

Status WriteListener::Close(){
  return this->listener_->Close();
}

WriteListener::~WriteListener(){
  delete this->listener_;
}


StatusOr<WriteSender *> WriteListener::AcceptSender(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  memory::Allocator *alloc;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  
  auto stat = initSender(this->listener_->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  AckReceiver *ack;
  ringbuffer::RemoteBuffer *rbuf;

  stat = connectSender(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up connection details");
  }

  return new WriteSender(ep, alloc, rbuf, ack);

}

StatusOr<WriteReceiver *> WriteListener::AcceptReceiver(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto stat = initReceiver(this->listener_->GetPdP(), opts, &rbuf, &ack);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing connection details");
  }
  
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  return new WriteReceiver(ep, rbuf, ack);
}

StatusOr<WriteConnection *> WriteListener::AcceptConnection(WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  if (opts.sender == kSenderWriteImm && opts.acknowledger == kAcknowledgerSend) {
      return Status(StatusCode::InvalidArgument, "duplex connection with WriteImm and Send Acknowledger not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;


  memory::Allocator *alloc;

  auto stat = initReceiver(this->listener_->GetPdP(), opts, &rbuf, &ack);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing receiver details");
  }
  stat = initSender(this->listener_->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_details local_conn_details;
  local_conn_details.opts = opts;
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();

  auto conn_opts = write_connection_opts;
  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  auto ep_s = this->listener_->Accept(conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  auto rcvr = new WriteReceiver(ep, rbuf, ack);

  AckReceiver *ackRcv;
  ringbuffer::RemoteBuffer *remote_rbuf;

  stat = connectSender(ep, *remote_conn_details, &remote_rbuf, &ackRcv);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up sender details");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  auto sndr = new WriteSender(ep, alloc, remote_rbuf, ackRcv);

  return new WriteConnection(sndr, rcvr);
}



/*
 *
 * DIALING
 *
 */


StatusOr<WriteSender*> DialWriteSender(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  memory::Allocator *alloc;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  auto stat = initSender(ep->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
   
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  AckReceiver *ack;
  ringbuffer::RemoteBuffer *rbuf;

  stat = connectSender(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up connection details");
  }

  return new WriteSender(ep, alloc, rbuf, ack);
}


StatusOr<WriteReceiver*> DialWriteReceiver(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;

  conn_details local_conn_details;
  local_conn_details.opts = opts;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();
  
  auto stat = initReceiver(ep->GetPdP(), opts, &rbuf, &ack);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing connection details");
  }
  
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  } 
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  return new WriteReceiver(ep, rbuf, ack);
}


StatusOr<WriteConnection*> DialWriteConnection(std::string ip, int port, WriteOpts opts){
  if (opts.sender == kSenderWrite && opts.buffer == kBufferBasic) {
      return Status(StatusCode::InvalidArgument, "write sender with basic buffer not supported");
  }
  if (opts.sender == kSenderWriteImm && opts.acknowledger == kAcknowledgerSend) {
      return Status(StatusCode::InvalidArgument, "duplex connection with WriteImm and Send Acknowledger not supported");
  }

  ringbuffer::Buffer *rbuf;
  Acknowledger *ack;

  memory::Allocator *alloc;

  auto conn_opts = write_connection_opts;

  auto ep_s = endpoint::Create(ip, port, conn_opts);
  if (!ep_s.ok()){
    return ep_s.status();
  }
  endpoint::Endpoint *ep = ep_s.value();

  auto stat = initReceiver(ep->GetPdP(), opts, &rbuf, &ack);
  if (!stat.ok()){
    return stat.Wrap("Error initalizing receiver details");
  }
  stat = initSender(ep->GetPdP(), opts, &alloc);
  if (!stat.ok()){
    return stat.Wrap("error initalizing sender details");
  }

  conn_details local_conn_details;
  local_conn_details.opts = opts;
  local_conn_details.ack_ctx = ack->GetContext();
  local_conn_details.buffer_ctx = rbuf->GetContext();

  conn_opts.private_data = &local_conn_details;
  conn_opts.private_data_len = sizeof(local_conn_details);

  stat = ep->Connect(conn_opts);
  if (!stat.ok()){
    return stat.Wrap("error connecting to remote");
  }
    
  conn_details *remote_conn_details;
  ep->GetConnectionInfo((void **)&remote_conn_details);

  if (remote_conn_details->opts.raw != opts.raw){
    ep->Close();
    return Status(StatusCode::InvalidArgument, "remote options missmatch");
  }

  auto rcvr = new WriteReceiver(ep, rbuf, ack);

  AckReceiver *ackRcv;
  ringbuffer::RemoteBuffer *remote_rbuf;

  stat = connectSender(ep, *remote_conn_details, &remote_rbuf, &ackRcv);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("error setting up sender details");
  }
  stat = connectReceiver(ep, *remote_conn_details, &rbuf, &ack);
  if (!stat.ok()){
    ep->Close();
    return stat.Wrap("Error connecting receiver");
  }

  auto sndr = new WriteSender(ep, alloc, remote_rbuf, ackRcv);

  return new WriteConnection(sndr, rcvr);

}

}
}
