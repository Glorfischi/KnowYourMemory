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
      ("unack",  "Number of messages that can be unacknowleged. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("1"))
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
  bool pingpong = flags["pingpong"].as<bool>();  


  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
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
    debug(stderr, "listening\n");
    auto conn_s = inst->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error Accepting " << conn_s.status() << std::endl;
      inst->Close();
      delete inst;
      exit(1);
    }
    auto conn = conn_s.value();
    debug(stderr, "accepted\n");
    
    if (lat) {
      stat = test_lat_recv(inst, count);
    } else if (pingpong) {
      stat = test_lat_pong(conn, inst, count, size);
    } else if (bw) {
      auto bw_s = test_bw_recv(inst, count, size);
      if (!bw_s.ok()){
        std::cerr << "Error running bench " << bw_s.status() << std::endl;
      }
      std::cerr << "## Bandwidth (MB/s)" << std::endl;
      std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
    }
    if (!stat.ok()){
      std::cerr << "Error sending " << stat << std::endl;
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
    kym::Status stat;
    if (lat) {
      stat = test_lat_send(conn, count, size, &measurements);
    } else if (pingpong) {
      stat = test_lat_ping(conn, inst, count, size, &measurements);
    } else if (bw) {
      auto bw_s = test_bw_send(conn, count, size, unack);
      if (!bw_s.ok()){
        std::cerr << "Error running bench " << bw_s.status() << std::endl;
      }
      std::cerr << "## Bandwidth (MB/s)" << std::endl;
      std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
    }

    if (!stat.ok()){
      std::cerr << "Error sending " << stat << std::endl;
    }
    conn->Close();
    delete conn;
  }

  if (!filename.empty()){
    std::ofstream file(filename);
    for (float f : measurements){
      file << f << "\n";
    }
    file.close();
  }


  inst->Close();
  delete inst;
  auto n = measurements.size();
  if (n == 0){
    return 0;
  }
  std::sort (measurements.begin(), measurements.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << measurements[q025] << "\t" << measurements[q500] << "\t" << measurements[q975] << std::endl;


  return 0;
}
 
