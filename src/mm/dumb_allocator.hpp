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

#include "error.hpp"
#include "mm.hpp"

namespace kym {
namespace memory {


class DumbAllocator : public Allocator {
  public:
    ~DumbAllocator() = default;
    DumbAllocator(struct ibv_pd pd);
    DumbAllocator(struct ibv_pd *pd);

    StatusOr<Region> Alloc(size_t size);
    Status Free(kym::memory::Region region);

  private:
    struct ibv_pd pd_;
};


}
}
#endif // KNY_MM_DUMB_ALLOCATOR_HPP_
