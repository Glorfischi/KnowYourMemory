/*
 *
 *  dumb_allocator.hpp
 *
 *  Implmentation of a very simple RDMA MR manager.
 *
 */

#ifndef KNY_MM_DUMB_ALLOCATOR_HPP_
#define KNY_MM_DUMB_ALLOCATOR_HPP_

#include <stddef.h>

#include <vector>

#include <infiniband/verbs.h>

#include "kym/mm.hpp"

namespace kym {
namespace memory {


class DumbAllocator : public Allocator {
  public:
    DumbAllocator(struct ibv_pd *pd);

    kym::memory::Region Alloc(size_t size);

    void Free(kym::memory::Region region);

    struct ibv_pd *pd_;
  private:
};


}
}
#endif // KNY_MM_DUMB_ALLOCATOR_HPP_

