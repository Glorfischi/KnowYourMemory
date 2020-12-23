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
      ("single-receiver",  "whether to have single receiver for all connections N:1" , cxxopts::value<bool>()->default_value("false"))
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
  bool singlercv = flags["single-receiver"].as<bool>();  

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<std::vector<float>*> measurements(conn_count);

  if (server){
    std::vector<std::thread> workers;
    std::vector<kym::connection::DirectWriteConnection*> conns;
    std::vector<kym::connection::Receiver *> recvrs;
    auto ln_s = kym::connection::ListenDirectWrite(ip, 9994);
    if (!ln_s.ok()){
      std::cerr << "Error listening " << ln_s.status().message() << std::endl;
      return 1;
    }
    auto ln = ln_s.value();

    for (int i = 0; i<conn_count; i++){
      auto conn_s = ln->Accept(size+sizeof(uint32_t));
      if (!conn_s.ok()){
        std::cerr << "Error accepting " << conn_s.status().message() << std::endl;
        return 1;
      }
      auto conn = conn_s.value();
      conns.push_back(conn);
      recvrs.push_back(conn);
    }

    if (!singlercv) {
      for (int i = 0; i<conn_count; i++){
        auto conn = conns[i];
        workers.push_back(std::thread([i, bw, pingpong, conn, count, size, &measurements](){
          set_core_affinity(i+2);
          std::vector<float> *m = new std::vector<float>();

          if (pingpong){
            auto stat = test_lat_pong(conn, conn, count, size);
            if (!stat.ok()){
              std::cerr << "Error running benchmark: " << stat << std::endl;
              return 1;
            }
          }
          if (bw) {
            auto bw_s = test_bw_recv(conn, count, size);
            if (!bw_s.ok()){
              std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
              return 1;
            }
            m->push_back(bw_s.value());
          }

          measurements[i] = m;
          std::chrono::milliseconds timespan(1000);
          std::this_thread::sleep_for(timespan);

          conn->Close();
          return 0;
        }));
      }
      for (int i = 0; i<workers.size(); i++){
        workers[i].join();
      }
    }
    if (singlercv) {
      set_core_affinity(1);
      if (bw) {
        std::vector<float> *m = new std::vector<float>();
        auto bw_s = test_bw_recv(recvrs, count, size);
        if (!bw_s.ok()){
          int cpu_num = sched_getcpu();
          std::cerr << "[CPU: " << cpu_num << "] Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        m->push_back(bw_s.value());
        measurements[0] = m;
      }
    }
    ln->Close();

  }
  if (client){
    std::chrono::milliseconds timespan(2000);
    std::this_thread::sleep_for(timespan);
    std::vector<std::thread> workers;
    std::vector<kym::connection::DirectWriteConnection*> conns;
    for (int i = 0; i<conn_count; i++){
      auto conn_s = kym::connection::DialDirectWrite(ip, 9994, size+sizeof(uint32_t));
      if (!conn_s.ok()){
        std::cerr << "Error dialing " << conn_s.status().message() << std::endl;
        return 1;
      }
      auto conn = conn_s.value();
      conns.push_back(conn);
    }

    for (int i = 0; i<conn_count; i++){
      auto conn = conns[i];
      workers.push_back(std::thread([i, bw, pingpong, conn, count, size, unack, &measurements](){
        set_core_affinity(i+2);
        std::vector<float> *m = new std::vector<float>();
        //std::chrono::milliseconds timespan(10);
        //std::this_thread::sleep_for(timespan);
        if (pingpong){
          auto stat = test_lat_ping(conn, conn, count, size, m);
          if (!stat.ok()){
            std::cerr << "Error running benchmark: " << stat << std::endl;
            return 1;
          }
        }

        if (bw) {
          auto bw_s = test_bw_send(conn, count, size, unack);
          if (!bw_s.ok()){
            std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
            return 1;
          }
          m->push_back(bw_s.value());
        }
        measurements[i] = m;
        //std::this_thread::sleep_for(timespan);
        conn->Close();
        delete conn;
        return 0;
      }));
      
    }
    for (int i = 0; i<workers.size(); i++){
      workers[i].join();
    }
  }
  if (lat || pingpong) {
    // Handle Latency distribution
    std::vector<float> joined;
    for (auto m : measurements){
      if (m != nullptr) {
        joined.reserve(m->size());
        joined.insert(joined.end(), m->begin(), m->end());
      }
    }
    if (!filename.empty()){
      std::ofstream file(filename);
      for (float f : joined){
        file << f << "\n";
      }
      file.close();
    }

    if (joined.size() > 0) {
      if (lat) {
        std::cout << "\tLatency";
      } else if(pingpong)  {
        std::cout << "\tPingPong Latency";
      }
      auto n = joined.size();
      std::cout << std::endl;
      std::cout << "N: " << n << std::endl;
      
      std::sort (joined.begin(), joined.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << joined[q025] << "\t" << joined[q500] << "\t" << joined[q975] << std::endl;
    }
  }

  if (bw) {
    uint64_t bandwidth = 0;
    for (auto m : measurements){
      if (m != nullptr) {
        bandwidth += m->at(0);
      }
    }
    std::cerr << "## Bandwidth (MB/s)" << std::endl;
    std::cout << (double)bandwidth/(1024*1024) << std::endl;
    if (!filename.empty()){
      std::ofstream file(filename, std::ios_base::app);
      file << conn_count <<  " " << size << " " << unack << " " << batch << " " <<  bandwidth << "\n";
      file.close();
    }
  }
  return 0;
}
 
