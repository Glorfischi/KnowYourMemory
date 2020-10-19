#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <fstream>
#include <ios>
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

#include "error.hpp"
#include "read.hpp"

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
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("60"))
      ("unack",  "Number of messages that can be unacknowleged. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("100"))
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
  int unack = flags["unack"].as<int>();  


  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> latency_m;

  std::cerr << "#### Testing ReadConnection ####" << std::endl;
  std::vector<float> measurements;
  if (server){
    auto ln_s = kym::connection::ListenRead(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::ReadListener *ln = ln_s.value();
    auto conn_s = ln->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error accepting : " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::ReadConnection *conn = conn_s.value();

    kym::Status stat;
    if (lat) {
      stat = test_lat_recv(conn, count, &measurements);
    } else if (bw) {
      auto bw_s = test_bw_recv(conn, count, size);
      if (!bw_s.ok()){
        std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
        return 1;
      }
      std::cerr << "## Bandwidth Receiver (MB/s)" << std::endl;
      std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
    } else {
      stat = test_lat_pong(conn, conn, count, size);
    }
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    }


    conn->Close();
    delete conn;
    ln->Close();
    delete ln;
  }

  if(client){
    std::chrono::milliseconds timespan(1000); // Make sure we received all acks
    std::this_thread::sleep_for(timespan);
      
    auto conn_s = kym::connection::DialRead(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::ReadConnection *conn = conn_s.value();

    std::this_thread::sleep_for(timespan);
    kym::Status stat;
    if (lat) {
      stat = test_lat_send(conn, count, size, &measurements);
      std::cerr << "## Latency Sender" << std::endl;
    } else if (bw) {
      auto bw_s = test_bw_send(conn, count, size, unack);
      if (!bw_s.ok()){
        std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
        return 1;
      }
      std::cerr << "## Bandwidth Sender (MB/s)" << std::endl;
      std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
    } else {
      stat = test_lat_ping(conn, conn, count, size, &measurements);
      std::cerr << "## Pingpong Sender" << std::endl;
    }
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    } 
    std::this_thread::sleep_for(timespan);
    conn->Close();
    delete conn;
  }
  
  if (measurements.size() == 0){
    return 0;
  }
  if (!filename.empty()){
    std::ofstream file(filename);
    for (float f : measurements){
      file << f << "\n";
    }
    file.close();
  }

  auto n = measurements.size();
  std::sort (measurements.begin(), measurements.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << measurements[q025] << "\t" << measurements[q500] << "\t" << measurements[q975] << std::endl;
  return 0;
}
 
