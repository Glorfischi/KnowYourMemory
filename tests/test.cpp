#include <cstring>
#include <iostream>

#include "kym/conn.hpp"
#include "kym/mm.hpp"
#include "kym/conn/send_receive.hpp"

void test_sender(kym::connection::Sender sender){
  kym::memory::Region region = sender.GetMemoryRegion(1024);
  memcpy(region.addr, (void *)"test", 5);
  sender.Send(region);
}


int main() {
  auto sender = kym::connection::DialSendReceive();
  test_sender(sender);
  return 0;
}
