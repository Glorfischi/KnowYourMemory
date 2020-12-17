#ifndef KNY_WRITE_ACKNOWLEDGER_HPP_
#define KNY_WRITE_ACKNOWLEDGER_HPP_


#include "endpoint.hpp"


#include "error.hpp"
#include "receive_queue.hpp"

namespace kym {
namespace connection {

struct AcknowledgerContext {
  uint32_t data[4];
};

class Acknowledger {
  public:
    virtual ~Acknowledger() = default;
    virtual kym::Status Close() = 0;

    virtual void Ack(uint32_t) = 0;
    virtual Status Flush() = 0;
    virtual AcknowledgerContext GetContext() = 0;
};


class AckReceiver {
  public:
    virtual ~AckReceiver() = default;
    virtual kym::Status Close() = 0;

    virtual kym::StatusOr<uint32_t> Get() = 0;
    // Will eventually update the head and will return a new head in this or subsequent calls
    virtual kym::StatusOr<uint32_t> GetEventually() = 0; 
};


class SendAcknowledger : public Acknowledger {
  public:
    SendAcknowledger(endpoint::Endpoint * ep) : ep_(ep){};
    SendAcknowledger(){};
    ~SendAcknowledger() = default;

    kym::Status Close();

    void Ack(uint32_t);
    Status Flush();

    AcknowledgerContext GetContext();
  private:
    endpoint::Endpoint *ep_; 

    uint32_t curr_offset_;
};
StatusOr<SendAcknowledger *> GetSendAcknowledger(endpoint::Endpoint *ep);

class SendAckReceiver : public AckReceiver {
  public:
    SendAckReceiver(endpoint::Endpoint *ep, endpoint::IReceiveQueue *rq) : ep_(ep), rq_(rq){}
    ~SendAckReceiver();
    Status Close();

    StatusOr<uint32_t> Get();
    StatusOr<uint32_t> GetEventually(){return this->Get();}; 
  private:
    endpoint::Endpoint *ep_; 
    endpoint::IReceiveQueue *rq_;

    uint32_t last_offset_ = 0;
};
StatusOr<SendAckReceiver *> GetSendAckReceiver(endpoint::Endpoint *ep);


class ReadAcknowledger : public Acknowledger {
  public:
    ReadAcknowledger(struct ibv_mr *mr);
    ~ReadAcknowledger() = default;

    kym::Status Close();

    void Ack(uint32_t);
    Status Flush();

    AcknowledgerContext GetContext();
  private:
    struct ibv_mr *mr_;

    volatile uint32_t *curr_offset_;
};
StatusOr<ReadAcknowledger *> GetReadAcknowledger(struct ibv_pd *pd);

class ReadAckReceiver : public AckReceiver {
  public:
    ReadAckReceiver(endpoint::Endpoint *ep, struct ibv_mr * mr, uint32_t key, uint64_t addr);
    ~ReadAckReceiver() = default;
    Status Close();

    StatusOr<uint32_t> Get();
    StatusOr<uint32_t> GetEventually();
  private:
    endpoint::Endpoint *ep_; 

    uint32_t key_;
    uint64_t addr_;

    struct ibv_mr *mr_;
    volatile uint32_t *curr_offset_;

    uint32_t last_offset_;
    uint32_t waiting_;
};
StatusOr<ReadAckReceiver *> GetReadAckReceiver(endpoint::Endpoint *ep, AcknowledgerContext ctx);





}
}

#endif // KNY_WRITE_ACKNOWLEDGER_HPP_
