#include "dumb_allocator.hpp"

#include <cstddef>
#include <cstdlib>

#include <iostream>

#include <infiniband/verbs.h>

#include "kym/mm.hpp"

kym::memory::DumbAllocator::DumbAllocator(struct ibv_pd *pd): pd_(pd){
}

kym::memory::Region kym::memory::DumbAllocator::Alloc(size_t size){
  void *buf = malloc(size);
  struct ibv_mr * mr = ibv_reg_mr(this->pd_, buf, size, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ);  
  kym::memory::Region reg = {0};
  reg.addr = buf;
  reg.length = size;
  reg.lkey = mr->lkey;
  reg.mr = mr;
  return reg;
}

void kym::memory::DumbAllocator::Free(kym::memory::Region region){
  ibv_dereg_mr(region.mr);
  free(region.addr);
  return;
}
