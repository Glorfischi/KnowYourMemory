#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include <infiniband/verbs.h>

#include "cxxopts.hpp"

#include "conn.hpp"
#include "mm.hpp"

#include "mm/dumb_allocator.hpp"
#include "conn/send_receive.hpp"
//#include "conn/shared_receive.hpp"
//#include "conn/read.hpp"

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
  auto reg = sender.GetMemoryRegion(4);
  if (!reg.ok()){
    std::cerr << "Sending Allocation failed! " << reg.status().message() << std::endl;
    return;
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  for (int i = 0; i < count; ++i){
    memcpy(reg.value().addr, (void *)&i, 4);
    //std::cout << i << std::endl;
    auto stat = sender.Send(reg.value());
    if(!stat.ok()) {
      std::cout << "ERROR " << stat.message() << std::endl;
      perror("err");
    }
    //std::chrono::milliseconds timespan(1); // This is because of a race condition...
    //std::this_thread::sleep_for(timespan);
  }
  auto t2 = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();
  sender.Free(reg.value());
  std::cout << "Sent  " << count << " requests in " << duration << " microseconds " <<  std::endl;
}


void test_receiver(kym::connection::Receiver &receiver, int count){
  auto reg = receiver.Receive();
  if (!reg.ok()){
    std::cerr << "Reading failed! " << reg.status().message() << std::endl;
    return;
  }
  receiver.Free(reg.value());
  auto t1 = std::chrono::high_resolution_clock::now();
  for (int i = 1; i < count; ++i){
    auto reg = receiver.Receive();
    if (!reg.ok()){
      std::cerr << "Reading failed! " << reg.status().message() << std::endl;
      return;
    }
    receiver.Free(reg.value());
  }
  auto t2 = std::chrono::high_resolution_clock::now();
  auto duration = std::chrono::duration_cast<std::chrono::microseconds>( t2 - t1 ).count();
  std::cout << "Received  " << count - 1 << " requests in " << duration << " microseconds " <<  std::endl;
}

void test_allocator(kym::memory::Allocator &allocator){
  auto reg = allocator.Alloc(1024);
  if (!reg.ok()){
    std::cerr << "Allocation failed! " << reg.status().message() << std::endl;
    return;
  }
  allocator.Free(reg.value());
  std::cout << "Allocated" << std::endl;
}

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  bool client = flags["client"].as<bool>();  

  std::cout << "#### Testing SendReceive 1:1 ####" << std::endl;
  if (client) {
    auto sr_conn = kym::connection::DialSendReceive("172.17.5.101", 9999);
    if (!sr_conn.ok()){
      std::cerr << "Error dialing send_receive connection" << sr_conn.status().message() << std::endl;
      return 1;
    }

    std::chrono::milliseconds timespan(1000); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);
    test_sender(*sr_conn.value(), 5000);
  } else  {
    auto ln = kym::connection::ListenSendReceive("172.17.5.101", 9999);
    if (!ln.ok()){
      std::cerr << "Error listening for send_receive" << ln.status().message() << std::endl;
      return 1;
    }
    auto conn = ln.value()->Accept();
    if (!conn.ok()){
      std::cerr << "Error accepting for send_receive" << conn.status().message() << std::endl;
      return 1;
    }
    test_receiver(*conn.value(), 5000);
  }
  /*std::cout << "#################################" << std::endl;
  std::cout << "#### Testing SharedReceive 1:1 ####" << std::endl;
  if (client) {
    conn = kym::connection::DialSharedReceive("172.17.5.101", 9998);

    std::chrono::milliseconds timespan(1000); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);
    test_sender(*conn, 5000);
  } else  {
    auto ln = kym::connection::ListenSharedReceive("172.17.5.101", 9998);
    conn = ln->Accept();
    test_receiver(*conn, 5000);
  }
  std::cout << "#################################" << std::endl;
  std::cout << "#### Testing Read 1:1 ####" << std::endl;
  if (client) {
    conn = kym::connection::DialRead("172.17.5.101", 9996);

    std::chrono::milliseconds timespan(1000); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);
    test_sender(*conn, 500);
    std::this_thread::sleep_for(timespan);
  } else  {
    auto ln = kym::connection::ListenRead("172.17.5.101", 9996);
    conn = ln->Accept();
    test_receiver(*conn, 500);
  }
  std::cout << "##########################" << std::endl;
  // TODO(fischi) Fails at 5th accept?
  std::cout << "#### Testing SharedReceive 5:5 ####" << std::endl;
  if (client) {
    for(int i = 0; i<4; i++){
      conn = kym::connection::DialSharedReceive("172.17.5.101", 9997);

      std::chrono::milliseconds timespan(1000); // This is because of a race condition...
      std::this_thread::sleep_for(timespan);
      test_sender(*conn, 5000);
    }
  } else  {
    auto ln = kym::connection::ListenSharedReceive("172.17.5.101", 9997);
    for(int i = 0; i<4; i++){
      conn = ln->Accept();
      test_receiver(*conn, 5000);
    }
  }
  std::cout << "#################################" << std::endl;*/
  /*std::cout << "#### Testing SharedReceive interleaf ####" << std::endl;
  if (client) {
    auto conn1 = kym::connection::DialSharedReceive("172.17.5.101", 9997);
    auto conn2 = kym::connection::DialSharedReceive("172.17.5.101", 9997);

    std::chrono::milliseconds timespan(1000); // This is because of a race condition...
    std::this_thread::sleep_for(timespan);
    kym::memory::Region region = conn1->GetMemoryRegion(4);
    int i = 1;
    memcpy(region.addr, (void *)&i, 4);
    int err = conn1->Send(region);
    if(err) {
      std::cout << "ERROR " << err << std::endl;
      perror("err");
    }
    err = conn1->Send(region);
    if(err) {
      std::cout << "ERROR " << err << std::endl;
      perror("err");
    }
    region = conn2->GetMemoryRegion(4);
    i = 2;
    memcpy(region.addr, (void *)&i, 4);
    err = conn2->Send(region);
    if(err) {
      std::cout << "ERROR " << err << std::endl;
      perror("err");
    }

  } else  {
    auto ln = kym::connection::ListenSharedReceive("172.17.5.101", 9997);
    auto conn1 = ln->Accept();
    auto conn2 = ln->Accept();
    kym::connection::ReceiveRegion region = conn1->Receive();
    std::cout << *(int *) region.addr << std::endl;
    region = conn2->Receive();
    std::cout << *(int *) region.addr << std::endl;

  }
  std::cout << "#########################################" << std::endl;*/

  return 0;
}
