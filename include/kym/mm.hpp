
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
  struct ibv_mr *mr;
};

class Allocator {
  public:
    virtual kym::memory::Region Alloc(size_t size) = 0;
    virtual void Free(kym::memory::Region region) = 0;
};


}
}
#endif // KNY_KNY_MM_H_

