#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include <ctime>
#include <iomanip>

#include "cxxopts.hpp"

#include "conn.hpp"
#include "bench/bench.hpp"

#include "write_atomic.hpp"

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
      ("source", "source IP address", cxxopts::value<std::string>()->default_value(""))
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("1024"))
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
  std::string src = flags["source"].as<std::string>();  

  std::string filename = flags["out"].as<std::string>();  


  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  


  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  
  int unack = flags["unack"].as<int>();  


  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> measurements;
  
  kym::connection::WriteAtomicInstance *inst = new kym::connection::WriteAtomicInstance();

  if (server){
    auto stat = inst->Listen(ip, 9999);
    if (!stat.ok()){
      std::cerr << "Error listening " << stat;
      inst->Close();
      delete inst;
      exit(1);
    }
    auto conn_s = inst->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error Accepting " << conn_s.status() << std::endl;
      inst->Close();
      delete inst;
      exit(1);
    }
    auto conn = conn_s.value();
    auto sr_s = conn->GetMemoryRegion(sizeof(int));
    if (!sr_s.ok()){
      std::cerr << "Error getting send region " << sr_s.status() << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }
    auto sr = sr_s.value();
    int data = 420;
    memcpy(sr.addr, &data, sizeof(data));
    auto rr_s = inst->Receive();
    if (!rr_s.ok()){
      std::cerr << "Error receiving " << rr_s.status() << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }
    std::cout << "GOT " << *(int *)rr_s.value().addr << std::endl;
 
    stat = conn->Send(sr);
    if (!stat.ok()){
      std::cerr << "Error sending " << stat << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }

    
    conn->Close();
    delete conn;
  }

  if(client){
    std::chrono::milliseconds timespan(1000); // If we start the server at the same time we will wait a little
    std::this_thread::sleep_for(timespan);
    auto conn_s = inst->Dial(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing " << conn_s.status() << std::endl;
      inst->Close();
      delete inst;
      return 1;
    }
    auto conn = conn_s.value();
    auto sr_s = conn->GetMemoryRegion(sizeof(int));
    if (!sr_s.ok()){
      std::cerr << "Error getting send region " << sr_s.status() << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }
    auto sr = sr_s.value();
    int data = 1337;
    memcpy(sr.addr, &data, sizeof(data));

    auto stat = conn->Send(sr);
    if (!stat.ok()){
      std::cerr << "Error sending " << stat << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }

    auto rr_s = inst->Receive();
    if (!rr_s.ok()){
      std::cerr << "Error receiving " << rr_s.status() << std::endl;
      conn->Close();
      inst->Close();
      delete conn;
      delete inst;
      exit(1);
    }
    std::cout << "GOT " << *(int *)rr_s.value().addr << std::endl;
    
    inst->Free(rr_s.value());
    conn->Close();
    delete conn;
  }

  if (!filename.empty()){
    std::cout << "writing" << std::endl;
    std::ofstream file(filename);
    for (float f : measurements){
      file << f << "\n";
    }
    file.close();
  }

  inst->Close();
  delete inst;

  return 0;
}
 
