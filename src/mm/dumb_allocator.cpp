#include "dumb_allocator.hpp"

#include <bits/stdint-uintn.h>
#include <cstddef>
#include <cstdlib>
#include <iostream>

#include <infiniband/verbs.h>

#include "mm.hpp"
namespace kym {
namespace memory {

DumbAllocator::DumbAllocator(struct ibv_pd pd): pd_(pd){
}

StatusOr<Region> DumbAllocator::Alloc(size_t size){
  void *buf = malloc(size);
  struct ibv_mr * mr = ibv_reg_mr(&this->pd_, buf, size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);  
  kym::memory::Region reg = {0};
  reg.addr = buf;
  reg.length = size;
  reg.lkey = mr->lkey;
  reg.context = (uint64_t)mr;
  return reg;
}

Status DumbAllocator::Free(Region region){
  int ret = ibv_dereg_mr((struct ibv_mr *)region.context);
  if (ret) {
    // TODO(Fischi) Map error codes
    return Status(StatusCode::Unknown, "free: error deregistering mr");
  }
  free(region.addr);
  return Status();
}

}
}
