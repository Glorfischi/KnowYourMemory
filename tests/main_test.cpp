#include <cstring>
#include <iostream>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include <infiniband/verbs.h>

#include "cxxopts.hpp"

#include "kym/conn.hpp"
#include "kym/mm.hpp"

#include "mm/dumb_allocator.hpp"
#include "conn/send_receive.hpp"

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "Test Binary");
  options
    .positional_help("[optional args]")
    .show_positional_help();
 
  try {
    options.add_options()
      ("client", "Wether to act as a client", cxxopts::value<bool>())
      ("server", "Wether to act as a server", cxxopts::value<bool>())
      ("address", "IP address to connect to", cxxopts::value<std::string>(), "IP")
      ("help", "Print help")
     ;
 
    auto result = options.parse(argc, argv);

    if (result.count("help")){
      std::cout << options.help({""}) << std::endl;
      exit(0);
    }
    return result;

  } catch (const cxxopts::OptionException& e) {
    std::cout << "error parsing options: " << e.what() << std::endl;
    std::cout << options.help({""}) << std::endl;
    exit(1);
  }
}
 


void test_sender(kym::connection::Sender &sender, int count){
  kym::memory::Region region = sender.GetMemoryRegion(4);
  for (int i = 0; i < count; ++i){
    //std::cout << i << std::endl;
    memcpy(region.addr, (void *)&i, 4);
    int err = sender.Send(region);
    if(err) {
      std::cout << "ERROR " << err << std::endl;
      perror("err");
    }
  }
  sender.Free(region);
}


void test_receiver(kym::connection::Receiver &receiver, int count){
  for (int i = 0; i < count; ++i){
    std::cout << i << std::endl;
    std::chrono::milliseconds timespan(10); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);
    kym::connection::ReceiveRegion region = receiver.Receive();
    receiver.Free(region);
  }
}

void test_allocator(kym::memory::Allocator &allocator){
  auto reg = allocator.Alloc(1024);
  allocator.Free(reg);
  std::cout << "Allocated" << std::endl;
}

int main(int argc, char* argv[]) {
  int dev_num;
  struct ibv_device **dev = ibv_get_device_list(&dev_num);
  struct ibv_context *ctx = ibv_open_device(*dev);
  struct ibv_pd *pd = ibv_alloc_pd(ctx);
  auto alloc = kym::memory::DumbAllocator(pd);
  test_allocator(alloc);

  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  bool client = flags["client"].as<bool>();  
  bool server = flags["server"].as<bool>();  

  if (client) {
    auto conn = kym::connection::DialSendReceive("172.17.5.101", 9999);

    std::chrono::milliseconds timespan(1000); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);

    test_sender(*conn, 500);
  } else if (server) {
    auto ln = kym::connection::ListenSendReceive("172.17.5.101", 9999);
    test_receiver(*ln->Accept(), 500);
  }

  
  return 0;
}
