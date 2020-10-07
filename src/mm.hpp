
/*
 *
 *  conn.h
 *
 *  Common interfaces for rdma memory management
 *
 */

#ifndef KNY_KNY_MM_H_
#define KNY_KNY_MM_H_

#include "error.hpp"
#include <stddef.h>

#include <infiniband/verbs.h>

namespace kym {
namespace memory {

struct Region {
  uint64_t    context;
	void *      addr;
	uint32_t    length;
	uint32_t    lkey;
};

class Allocator {
  public:
    virtual ~Allocator() = default;

    virtual StatusOr<kym::memory::Region> Alloc(size_t size) = 0;
    virtual Status Free(kym::memory::Region region) = 0;
};


}
}
#endif // KNY_KNY_MM_H_
