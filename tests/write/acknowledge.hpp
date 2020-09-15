#ifndef KNY_WRITE_ACKNOWLEDGER_HPP_
#define KNY_WRITE_ACKNOWLEDGER_HPP_


#include "error.hpp"
#include <bits/stdint-uintn.h>

namespace kym {
namespace connection {

struct AcknowledgerContext {
  uint32_t data[4];
};

class Acknowledger {
  public:
    virtual ~Acknowledger() = 0;
    virtual kym::Status Close() = 0;

    virtual void Ack(uint32_t) = 0;
    virtual AcknowledgerContext GetContext() = 0;
};


class AckReceiver {
  public:
    virtual ~AckReceiver() = 0;
    virtual kym::Status Close() = 0;

    virtual kym::StatusOr<uint32_t> Get() = 0;
};

}
}

#endif // KNY_WRITE_ACKNOWLEDGER_HPP_
