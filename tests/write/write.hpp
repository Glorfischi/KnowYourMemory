
#ifndef KNY_WRITE_HPP_
#define KNY_WRITE_HPP_

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
#include "receive_queue.hpp"
#include "ring_buffer/ring_buffer.hpp"

namespace kym {
namespace connection {

const uint8_t kAllocatorDumb = 0;

const uint8_t kAcknowledgerSend = 0;
const uint8_t kAcknowledgerRead = 1;

const uint8_t kSenderWrite = 0;
const uint8_t kSenderWriteCRC = 0;
const uint8_t kSenderWriteOffset = 2;
const uint8_t kSenderWriteImm = 1;
const uint8_t kSenderWriteAtomic = 3;
const uint8_t kSenderWriteReverse = 4;

const uint8_t kBufferBasic = 0;
const uint8_t kBufferMagic = 1;
const uint8_t kBufferReverse = 2;

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
    WriteReceiver(endpoint::Endpoint *ep, bool owns_ep, ringbuffer::Buffer *rbuf, Acknowledger *ack);
    WriteReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack);
    ~WriteReceiver();

    Status Close();


    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  protected:
    endpoint::Endpoint *ep_;
    bool owns_ep_;
    ringbuffer::Buffer *rbuf_;
    Acknowledger *ack_;

    uint32_t acked_;
    uint32_t length_;
    uint32_t max_unacked_;
};

class WriteSender : public Sender, public BatchSender {
  public:
    WriteSender(endpoint::Endpoint *ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : ep_(ep), owns_ep_(true), alloc_(alloc), rbuf_(rbuf), ack_(ack), ackd_id_(0), next_id_(1) {};
    WriteSender(endpoint::Endpoint *ep, bool owns_ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : ep_(ep), owns_ep_(owns_ep), alloc_(alloc), rbuf_(rbuf), ack_(ack), ackd_id_(0), next_id_(1) {};
    ~WriteSender();

    Status Close();


    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions);
    Status Wait(uint64_t id);
    Status Free(SendRegion region);
  protected:
    endpoint::Endpoint *ep_;
    bool owns_ep_;
    memory::Allocator *alloc_;
    ringbuffer::RemoteBuffer *rbuf_;
    AckReceiver *ack_;

    uint64_t ackd_id_;
    uint64_t next_id_;


};

class WriteOffsetReceiver : public WriteReceiver {
  public:
    WriteOffsetReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack, struct ibv_mr *tail_mr)
      : WriteReceiver(ep, rbuf, ack), tail_mr_(tail_mr), tail_((volatile uint32_t *)tail_mr->addr) {};
    WriteOffsetReceiver(endpoint::Endpoint *ep, bool owns_ep, ringbuffer::Buffer *rbuf, Acknowledger *ack, struct ibv_mr *tail_mr)
      : WriteReceiver(ep, owns_ep, rbuf, ack), tail_mr_(tail_mr), tail_((volatile uint32_t *)tail_mr->addr) {};
    Status Close();

    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    struct ibv_mr *tail_mr_;
    volatile uint32_t *tail_;
};

class WriteOffsetSender : public WriteSender {
  public:
    WriteOffsetSender(endpoint::Endpoint *ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack, 
        uint64_t tail_addr, uint32_t tail_key) 
      : WriteSender(ep, alloc, rbuf, ack), tail_addr_(tail_addr), tail_key_(tail_key) {};
    WriteOffsetSender(endpoint::Endpoint *ep, bool owns_ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, 
        AckReceiver *ack, uint64_t tail_addr, uint32_t tail_key) 
      : WriteSender(ep, owns_ep, alloc, rbuf, ack), tail_addr_(tail_addr), tail_key_(tail_key) {};


    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions);
    Status Wait(uint64_t id);
    Status Free(SendRegion region);

  private:
    // TODO(Fischi): Write inline
    uint64_t tail_addr_;
    uint32_t tail_key_;

};

class WriteImmReceiver : public WriteReceiver {
  public:
    WriteImmReceiver(endpoint::Endpoint *ep, ringbuffer::Buffer *rbuf, Acknowledger *ack, endpoint::IReceiveQueue *rq)
      : WriteReceiver(ep, rbuf, ack), rq_(rq) {};
    WriteImmReceiver(endpoint::Endpoint *ep, bool owns_ep, ringbuffer::Buffer *rbuf, Acknowledger *ack, endpoint::IReceiveQueue *rq)
      : WriteReceiver(ep, owns_ep, rbuf, ack), rq_(rq) {};
    Status Close();
    StatusOr<ReceiveRegion> Receive();
    Status Free(ReceiveRegion);
  private:
    endpoint::IReceiveQueue *rq_;
};

class WriteImmSender : public WriteSender {
  public:
    WriteImmSender(endpoint::Endpoint *ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : WriteSender(ep, alloc, rbuf, ack) {};
    WriteImmSender(endpoint::Endpoint *ep, bool owns_ep, memory::Allocator *alloc, ringbuffer::RemoteBuffer *rbuf, AckReceiver *ack) 
      : WriteSender(ep, owns_ep, alloc, rbuf, ack) {};


    StatusOr<SendRegion> GetMemoryRegion(size_t size);
    Status Send(SendRegion region);
    StatusOr<uint64_t> SendAsync(SendRegion region);
    Status Send(std::vector<SendRegion> regions);
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions);
    Status Wait(uint64_t id);
    Status Free(SendRegion region);
};

class WriteConnection : public Connection, public BatchSender {
  public:
    WriteConnection(WriteSender *snd, WriteReceiver *rcv) 
      : ep_(nullptr), snd_(snd), rcv_(rcv) {};
    WriteConnection(endpoint::Endpoint *ep, WriteSender *snd, WriteReceiver *rcv) 
      : ep_(ep), snd_(snd), rcv_(rcv) {};

    ~WriteConnection();

    Status Close();

    StatusOr<ReceiveRegion> Receive(){return this->rcv_->Receive();};
    Status Free(ReceiveRegion reg){return this->rcv_->Free(reg);};


    StatusOr<SendRegion> GetMemoryRegion(size_t size){return this->snd_->GetMemoryRegion(size);};

    Status Send(SendRegion region){return this->snd_->Send(region);};
    StatusOr<uint64_t> SendAsync(SendRegion region){return this->snd_->SendAsync(region);};

    Status Send(std::vector<SendRegion> regions){return this->snd_->Send(regions);};
    StatusOr<uint64_t> SendAsync(std::vector<SendRegion> regions){return this->snd_->SendAsync(regions);};

    Status Wait(uint64_t id){return this->snd_->Wait(id);};

    Status Free(SendRegion region){return this->snd_->Free(region);};
  private:
    endpoint::Endpoint *ep_;
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
