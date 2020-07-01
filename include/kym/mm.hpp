
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma memory management
 *
 */

#ifndef KNY_KNY_MM_H_
#define KNY_KNY_MM_H_

#include <stddef.h>

#include <infiniband/verbs.h>

namespace kym {
namespace memory {

struct Region {
	void *      addr;
	uint32_t    length;
	uint32_t    lkey;
};

class Allocator {
  public:
    virtual Region Alloc(size_t size);
    virtual void Free(Region region);
};


}
}
#endif // KNY_KNY_MM_H_

