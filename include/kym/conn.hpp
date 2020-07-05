
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

struct ReceiveRegion {
  int         id;
	void *      addr;
	uint32_t    length;
};

/*
 * Interfaces
 */
class Sender {
  public:
    virtual kym::memory::Region GetMemoryRegion(size_t size) = 0;
    virtual void Free(kym::memory::Region region) = 0;
    virtual int Send(kym::memory::Region region) = 0;
};

class Receiver {
  public:
    virtual ReceiveRegion Receive() = 0;
    virtual void Free(ReceiveRegion region) = 0;
};


class Connection: public Sender, public Receiver {};

}
}

#endif // KNY_KNY_CONN_H_

