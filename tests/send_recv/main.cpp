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

#include "send_receive.hpp"

#include "debug.h"



cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("srq", "Whether to use a shard receive queue", cxxopts::value<bool>())
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

  bool srq = flags["srq"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  
  int unack = flags["unack"].as<int>();  


  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> measurements;

  if (server){
      
      kym::connection::SendReceiveConnection *conn;
      kym::connection::SendReceiveListener *ln;
      if (srq){
        auto ln_s = kym::connection::ListenSharedReceive(ip, 9999);
        if (!ln_s.ok()){
          std::cerr << "Error listening for send_receive with shared receive queue " << ln_s.status().message() << std::endl;
          return 1;
        }
        ln = ln_s.value();
      } else {
        auto ln_s = kym::connection::ListenSendReceive(ip, 9999);
        if (!ln_s.ok()){
          std::cerr << "Error listening for send_receive " << ln_s.status().message() << std::endl;
          return 1;
        }
        ln = ln_s.value();
      }
      auto conn_s = ln->Accept();
      if (!conn_s.ok()){
        std::cerr << "Error accepting for send_receive " << conn_s.status().message() << std::endl;
        return 1;
      }
      
      conn = conn_s.value();

      if (bw) {
        auto bw_s = test_bw_recv(conn, count, size);
        if (!bw_s.ok()){
          std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        std::cerr << "## Bandwidth Receiver (MB/s)" << std::endl;
        std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
      } else if (lat) {
        auto stat = test_lat_recv(conn, count, &measurements);
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        }
      } else {
        auto stat = test_lat_pong(conn, conn, count, size);
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        }
      }
      conn->Close();
      ln->Close();
  }

  if(client){
    std::chrono::milliseconds timespan(4000); // If we start the server at the same time we will wait a little
    std::this_thread::sleep_for(timespan);
    auto conn_s = kym::connection::DialSendReceive(ip, 9999, src);
    if (!conn_s.ok()){
      std::cerr << "Error dialing send_receive co_nection" << conn_s.status().message() << std::endl;
      return 1;
    }
    kym::connection::SendReceiveConnection *conn = conn_s.value();

    if (bw) {
      std::chrono::milliseconds timespan(1000); // This is because of a race condition...
      std::this_thread::sleep_for(timespan);
      if (batch > 1){
        auto bw_s = test_bw_batch_send(conn, count, size, batch, unack);
        if (!bw_s.ok()){
          std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        std::cerr << "## Bandwidth Sender (MB/s)" << std::endl;
        std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
      } else {
        auto bw_s = test_bw_send(conn, count, size, unack);
        if (!bw_s.ok()){
          std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        std::cerr << "## Bandwidth Sender (MB/s)" << std::endl;
        std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
      }
    } else if (lat) {
      std::chrono::milliseconds timespan(1000); // This is because of a race condition...
      std::this_thread::sleep_for(timespan);

      measurements.reserve(count);

      auto stat = test_lat_send(conn, count, size, &measurements);
      if (!stat.ok()){
        std::cerr << "Error running benchmark: " << stat << std::endl;
        return 1;
      }

      auto n = count;
      std::vector<float> sorted = measurements;
      std::sort (sorted.begin(), sorted.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "## Latency Sender" << std::endl;
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << sorted[q025] << "\t" << sorted[q500] << "\t" << sorted[q975] << std::endl;
    } else {
      // pingpong
      std::chrono::milliseconds timespan(1000); // This is because of a race condition...
      std::this_thread::sleep_for(timespan);

      measurements.reserve(count);

      auto stat = test_lat_ping(conn, conn, count, size, &measurements);
      if (!stat.ok()){
        std::cerr << "Error running benchmark: " << stat << std::endl;
        return 1;
      }
      
      auto n = count;
      std::vector<float> sorted = measurements;
      std::sort (sorted.begin(), sorted.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "## Ping Pong latency" << std::endl;
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << sorted[q025] << "\t" << sorted[q500] << "\t" << sorted[q975] << std::endl;
    }
    conn->Close();
  }

  if (!filename.empty()){
    std::cout << "writing" << std::endl;
    std::ofstream file(filename);
    for (float f : measurements){
      file << f << "\n";
    }
    file.close();
  }


    
  return 0;
}
 
