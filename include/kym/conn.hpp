
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma connections
 *
 */

#ifndef KNY_KNY_CONN_H_
#define KNY_KNY_CONN_H_

#include <stddef.h>

#include "kym/mm.hpp"

namespace kym {
namespace connection {

/*
 * Interfaces
 */
class Sender {
  public:
    virtual kym::memory::Region GetMemoryRegion(size_t size);
    virtual int Send(kym::memory::Region region);
};

class Receiver {
  public:
    virtual kym::memory::Region Receive();
    virtual void Free(kym::memory::Region region);
};


class Connection: public Sender, public Receiver {};

}
}

#endif // KNY_KNY_CONN_H_

