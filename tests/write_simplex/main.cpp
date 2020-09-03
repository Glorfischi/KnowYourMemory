#include <cstdint>
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
      ("source", "source IP address", cxxopts::value<std::string>()->default_value(""))
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
  std::string src = flags["source"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  


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

    if (lat){
      std::vector<float> latency_us;
      test_lat_recv(conn, count, &latency_us);
      auto n = count;
      std::sort (latency_us.begin(), latency_us.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << latency_us[q025] << "\t" << latency_us[q500] << "\t" << latency_us[q975] << std::endl;
    } else {
      std::vector<float> bw_bps;
      auto stat = test_bw_batch_recv(conn, count, size, batch, &bw_bps);
      if (!stat.ok()){
        std::cerr << "Error running benchmark: " << stat << std::endl;
        return 1;
      }
      auto n = bw_bps.size();
      std::sort (bw_bps.begin(), bw_bps.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      float sum = 0;
      for (float b : bw_bps){
        sum += b;
      }
      std::cout << "## Bandwidth Receiver (MB/s)" << std::endl;
      std::cout << "mean: " << (sum/n)/(1024*1024) << std::endl;
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << bw_bps[q025]/(1024*1024) << "\t" << bw_bps[q500]/(1024*1024) << "\t" << bw_bps[q975]/(1024*1024) << std::endl;
    }
    
    conn->Close();
    ln->Close();
  } 
  
  std::thread client_thread;
  if (client){
    auto conn_s = kym::connection::DialWriteSimplex(ip, 9999, src);
    if (!conn_s.ok()){
      std::cerr << "Error dialing send_receive connection" << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    if (lat) {
      std::vector<float> latency_us;
      test_lat_send(conn, count, size, &latency_us);
      auto n = count;
      std::sort (latency_us.begin(), latency_us.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << latency_us[q025] << "\t" << latency_us[q500] << "\t" << latency_us[q975] << std::endl;
    } else {
      std::vector<float> bw_bps;
      auto stat = test_bw_batch_send(conn, count, size, batch, &bw_bps);
      if (!stat.ok()){
        std::cerr << "Error running benchmark: " << stat << std::endl;
        return 1;
      }
       auto n = bw_bps.size();
      std::sort (bw_bps.begin(), bw_bps.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      float sum = 0;
      for (float b : bw_bps){
        sum += b;
      }
      std::cout << "## Bandwidth Sender (MB/s)" << std::endl;
      std::cout << "mean: " << (sum/n)/(1024*1024) << std::endl;
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << bw_bps[q025]/(1024*1024) << "\t" << bw_bps[q500]/(1024*1024) << "\t" << bw_bps[q975]/(1024*1024) << std::endl;
    }

    std::chrono::milliseconds timespan(1000); // We need to wait for the last ack to come in. o/w reciever will fail. This is hacky..
    std::this_thread::sleep_for(timespan);
    conn->Close();
  } 
   
  return 0;
}
 
