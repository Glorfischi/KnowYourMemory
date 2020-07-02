#include <cstring>
#include <infiniband/verbs.h>
#include <iostream>
#include <ostream>

//#include "kym/conn.hpp"
#include "kym/mm.hpp"

#include "mm/dumb_allocator.hpp"
//#include "kym/conn/send_receive.hpp"

/*void test_sender(kym::connection::Sender sender){
  kym::memory::Region region = sender.GetMemoryRegion(1024);
  memcpy(region.addr, (void *)"test", 5);
  sender.Send(region);
}*/

void test_allocator(kym::memory::Allocator &allocator){
  auto reg = allocator.Alloc(1024);
  allocator.Free(reg);
  std::cout << "Allocated" << std::endl;
}

int main() {
  int dev_num;
  struct ibv_device **dev = ibv_get_device_list(&dev_num);
  struct ibv_context *ctx = ibv_open_device(*dev);
  struct ibv_pd *pd = ibv_alloc_pd(ctx);
  auto alloc = kym::memory::DumbAllocator(pd);
  test_allocator(alloc);
  return 0;
}
