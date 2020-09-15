
#ifndef KNY_WRITE_HPP_
#define KNY_WRITE_HPP_

#include <bits/stdint-uintn.h>
#include <cstdint>
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

#include "acknowledge.hpp"
#include "ring_buffer/ring_buffer.hpp"

namespace kym {
namespace connection {

const uint8_t kAllocatorDumb = 0;

const uint8_t kAcknowledgerSend = 0;
const uint8_t kAcknowledgerRead = 1;

const uint8_t kSenderWrite = 0;
const uint8_t kSenderWriteImm = 1;

const uint8_t kBufferBasic = 0;
const uint8_t kBufferMagic = 1;

typedef union {
        struct {
                unsigned allocator:3;
                unsigned acknowledger:3;
                unsigned sender:3;
                unsigned buffer:3;
        };
        uint16_t raw;
} WriteOpts;

class WriteReceiver : public Receiver {
  public:
    WriteReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack) 
      : ep_(ep), rbuf_(rbuf), ack_(ack){};
    ~WriteReceiver();

    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    endpoint::Endpoint *ep_;
    ringbuffer::Buffer *rbuf_;
    Acknowledger *ack_;
};

class WriteSender : public Sender, public BatchSender {
  public:
    WriteSender(endpoint::Endpoint *ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : ep_(ep), alloc_(alloc), rbuf_(rbuf), ack_(ack) {};
    ~WriteSender();

    Status Close();

    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    Status Free(SendRegion region);
  private:
    endpoint::Endpoint *ep_;
    memory::Allocator *alloc_;
    ringbuffer::RemoteBuffer *rbuf_;
    AckReceiver *ack_;
};

class WriteConnection : public Connection {
  public:
    WriteConnection(WriteSender *snd, WriteReceiver *rcv) 
      : snd_(snd), rcv_(rcv) {};
    ~WriteConnection();

    Status Close();

    StatusOr<ReceiveRegion> Receive(){return this->rcv_->Receive();};
    Status Free(ReceiveRegion reg){return this->rcv_->Free(reg);};

    StatusOr<SendRegion> GetMemoryRegion(size_t size){return this->snd_->GetMemoryRegion(size);};
    Status Send(SendRegion region){return this->snd_->Send(region);};
    Status Send(std::vector<SendRegion> regions){return this->snd_->Send(regions);};
    Status Free(SendRegion region){return this->snd_->Free(region);};
  private:
    WriteSender *snd_;
    WriteReceiver *rcv_;
};
StatusOr<WriteConnection*> DialWriteConnection(std::string ip, int port, WriteOpts opts);
StatusOr<WriteSender*> DialWriteSender(std::string ip, int port, WriteOpts opts);
StatusOr<WriteReceiver*> DialWriteReceiver(std::string ip, int port, WriteOpts opts);

class WriteListener {
  public:
    WriteListener(endpoint::Listener *listener)
      : listener_(listener){};
    ~WriteListener();

    Status Close();

    StatusOr<WriteConnection*> AcceptConnection(WriteOpts);
    StatusOr<WriteSender*> AcceptSender(WriteOpts);
    StatusOr<WriteReceiver*> AcceptReceiver(WriteOpts);
  private:
    endpoint::Listener *listener_;
};

StatusOr<WriteListener*> ListenWrite(std::string ip, int port);

}
}

#endif // KNY_WRITE_HPP_
