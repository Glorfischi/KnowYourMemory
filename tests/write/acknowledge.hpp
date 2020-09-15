#ifndef KNY_WRITE_ACKNOWLEDGER_HPP_
#define KNY_WRITE_ACKNOWLEDGER_HPP_


#include "endpoint.hpp"

#include <bits/stdint-uintn.h>

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
    virtual ~AckReceiver() = 0;
    virtual kym::Status Close() = 0;

    virtual kym::StatusOr<uint32_t> Get() = 0;
};


class SendAcknowledger : public Acknowledger {
  public:
    SendAcknowledger(endpoint::Endpoint * ep) : ep_(ep){};
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

class SendAckReceiver {
  public:
    SendAckReceiver(endpoint::Endpoint *ep, endpoint::IReceiveQueue *rq) : ep_(ep), rq_(rq){}
    ~SendAckReceiver();
    Status Close();

    StatusOr<uint32_t> Get();
  private:
    endpoint::Endpoint *ep_; 
    endpoint::IReceiveQueue *rq_;
};
StatusOr<SendAckReceiver *> GetSendAckReceiver(endpoint::Endpoint *ep);




}
}

#endif // KNY_WRITE_ACKNOWLEDGER_HPP_
