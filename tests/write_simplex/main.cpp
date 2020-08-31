#include <bits/stdint-uintn.h>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include "cxxopts.hpp"

#include "conn.hpp"
#include "bench/bench.hpp"

#include "write_simplex.hpp"

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "writesimplex");
  try {
  options.add_options()
      ("bw", "Whether to test bandwidth", cxxopts::value<bool>())
      ("lat", "Whether to test latency", cxxopts::value<bool>())
      ("client", "Whether to as client only", cxxopts::value<bool>())
      ("server", "Whether to as server only", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("1024"))
      ("batch",  "Number of messages to send in a single batch. Only relevant for bandwidth benchmark", 
       cxxopts::value<int>()->default_value("20"))
     ;
 
    auto result = options.parse(argc, argv);
    if (!result.count("address")) {
      std::cerr << options.help({""}) << std::endl;
      exit(1);
    }
    if (!result.count("lat") && !result.count("bw")) {
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

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> latency_us;

  std::cout << "#### Testing WriteSimplex ####" << std::endl;

  std::thread server_thread;
  if (server){
    auto ln_s = kym::connection::ListenWriteSimplex(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening for send_receive " << ln_s.status().message() << std::endl;
      return 1;
    }
    auto ln = ln_s.value();
    auto conn_s = ln->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error accepting for send_receive " << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    test_lat_recv(conn, count, &latency_us);
    
    conn->Close();
    ln->Close();
  } 
  
  std::thread client_thread;
  if (client){
    auto conn_s = kym::connection::DialWriteSimplex(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error dialing send_receive connection" << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    test_lat_send(conn, count, size, &latency_us);

    std::chrono::milliseconds timespan(1000); // We need to wait for the last ack to come in. o/w reciever will fail. This is hacky..
    std::this_thread::sleep_for(timespan);
    conn->Close();
  } 
  auto n = count;
  std::sort (latency_us.begin(), latency_us.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << latency_us[q025] << "\t" << latency_us[q500] << "\t" << latency_us[q975] << std::endl;
  
  return 0;
}
 
