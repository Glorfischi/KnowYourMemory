#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <infiniband/verbs.h>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include <ctime>
#include <iomanip>
#include <vector>

#include "cxxopts.hpp"

#include "conn.hpp"
#include "bench/bench.hpp"
#include "async_events.hpp"

#include "direct_write.hpp"

#include "debug.h"


cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("bw", "Whether to test bandwidth", cxxopts::value<bool>())
      ("lat", "Whether to test latency", cxxopts::value<bool>())
      ("pingpong", "Whether to test pingpong latency", cxxopts::value<bool>())
      ("client", "Whether to as client only", cxxopts::value<bool>())
      ("server", "Whether to as server only", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("1024"))
      ("c,conn",  "Number of connections" , cxxopts::value<int>()->default_value("1"))
      ("unack",  "Number of messages that can be unacknowleged. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("100"))
      ("batch",  "Number of messages to send in a single batch. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("1"))
      ("out", "filename to output measurements", cxxopts::value<std::string>()->default_value(""))
     ;
 
    auto result = options.parse(argc, argv);
    if (!result.count("address")) {
      std::cerr << "Specify an address" << std::endl;
      std::cerr << options.help({""}) << std::endl;
      exit(1);
    }

    if (!result.count("lat") && !result.count("bw") && !result.count("pingpong")) {
      std::cerr << "Either perform latency or bandwidth benchmark" << std::endl;
      std::cerr << options.help({""}) << std::endl;
      exit(1);
    }
    return result;
  } catch (const cxxopts::OptionException& e) {
    std::cerr << "error parsing options: " << e.what() << std::endl;
    std::cerr << options.help({""}) << std::endl;
    exit(1);
  }
}

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  

  std::string filename = flags["out"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  
  bool pingpong = flags["pingpong"].as<bool>();  


  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  
  int unack = flags["unack"].as<int>();  

  int conn_count = flags["conn"].as<int>();  

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  if (server){
    auto ln_s = kym::connection::ListenDirectWrite(ip, 9994);
    if (!ln_s.ok()){
      std::cerr << "Error listening " << ln_s.status().message() << std::endl;
      return 1;
    }
    auto ln = ln_s.value();

    auto conn_s = ln->Accept(size+sizeof(uint32_t));
    if (!conn_s.ok()){
      std::cerr << "Error accepting " << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();
    
    auto buf_s = conn->GetMemoryRegion(size);
    if (!buf_s.ok()){
      std::cerr << "Error reg buffer " << buf_s.status().message() << std::endl;
      return 1;
    }
    auto buf = buf_s.value();
    
    auto reg_s = conn->Receive();
    if (!reg_s.ok()){
      std::cerr << "Error receiving " << reg_s.status().message() << std::endl;
      return 1;
    }
    debug(stderr, "Main: Received\n");
    auto stat = conn->Send(buf);
    if (!stat.ok()){
      std::cerr << "Error sending " << stat.message() << std::endl;
      return 1;
    }
    debug(stderr, "Main: Sent\n");

    conn->Close();
    ln->Close();

  }
  if (client){
    auto conn_s = kym::connection::DialDirectWrite(ip, 9994, size+sizeof(uint32_t));
    if (!conn_s.ok()){
      std::cerr << "Error dialing " << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    auto buf_s = conn->GetMemoryRegion(size);
    if (!buf_s.ok()){
      std::cerr << "Error reg buffer " << buf_s.status().message() << std::endl;
      return 1;
    }
    auto buf = buf_s.value();
    
    auto stat = conn->Send(buf);
    if (!stat.ok()){
      std::cerr << "Error sending " << stat.message() << std::endl;
      return 1;
    }
    debug(stderr, "Main: Sent\n");
    auto reg_s = conn->Receive();
    if (!reg_s.ok()){
      std::cerr << "Error receiving " << reg_s.status().message() << std::endl;
      return 1;
    }
    debug(stderr, "Main: Received\n");
    
    conn->Close();
  }
  return 0;
}
 
