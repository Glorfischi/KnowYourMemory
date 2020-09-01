#include <bits/stdint-uintn.h>
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

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("srq", "Whether to use a shard receive queue", cxxopts::value<bool>())
      ("bw", "Whether to test bandwidth", cxxopts::value<bool>())
      ("lat", "Whether to test latency", cxxopts::value<bool>())
      ("client", "Whether to as client only", cxxopts::value<bool>())
      ("server", "Whether to as server only", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("1024"))
      ("batch",  "Number of messages to send in a single batch. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("20"))
     ;
 
    auto result = options.parse(argc, argv);
    if (!result.count("address")) {
      std::cerr << "Specify an address" << std::endl;
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




void sr_test_bw_send(kym::connection::SendReceiveConnection *conn, int count, int size, int batch){
  std::chrono::milliseconds timespan(1000); // This is because of a race condition...
  std::this_thread::sleep_for(timespan);

  int batch_size = batch;
  std::vector<kym::connection::SendRegion> bufs;
  for (int i = 0; i<batch_size; i++){
    auto buf_s = conn->GetMemoryRegion(size);
    if (!buf_s.ok()){
      std::cerr << "Error allocating send region " << buf_s.status().message() << std::endl;
      return;
    }
    bufs.push_back(buf_s.value());
  }
  for(int i = 0; i<count/batch_size; i++){
    //(*(uint64_t*)buf.addr) = i; 
    // std::cout << "send " << i << std::endl;
    auto send_s = conn->Send(bufs);
    if (!send_s.ok()){
      std::cerr << "Error sending buffer " << send_s.message() << std::endl;
      break;
    }
  }
  for (auto buf : bufs){
    conn->Free(buf);
  }

  std::this_thread::sleep_for(timespan);
}

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  

  bool srq = flags["srq"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  


  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> latency_m;

  std::cout << "#### Testing SendReceive ####" << std::endl;

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

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark only CPU i as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(0, &cpuset);
      int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
      }
      conn = conn_s.value();

      if (bw) {
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
        std::cout << bw_bps[q025] << "\t" << bw_bps[q500] << "\t" << bw_bps[q975] << std::endl;

      } else if (lat) {
        std::vector<float> latency_us;
        auto stat = test_lat_recv(conn, count, &latency_us);
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        }
      }
      conn->Close();
      ln->Close();
      return 0;
  }

  if(client){
      auto conn_s = kym::connection::DialSendReceive(ip, 9999);
      if (!conn_s.ok()){
        std::cerr << "Error dialing send_receive co_nection" << conn_s.status().message() << std::endl;
        return 1;
      }
      kym::connection::SendReceiveConnection *conn = conn_s.value();

      // Create a cpu_set_t object representing a set of CPUs. Clear it and mark only CPU i as set.
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(1, &cpuset);
      int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
      if (rc != 0) {
        std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
        return 1;
      }

      if (bw) {
        std::chrono::milliseconds timespan(1000); // This is because of a race condition...
        std::this_thread::sleep_for(timespan);

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
        std::cout << bw_bps[q025] << "\t" << bw_bps[q500] << "\t" << bw_bps[q975] << std::endl;

      } else {
        std::chrono::milliseconds timespan(1000); // This is because of a race condition...
        std::this_thread::sleep_for(timespan);

        auto t = std::time(nullptr);
        auto tm = *std::localtime(&t);
        std::stringstream tmstream;
        tmstream << std::put_time(&tm, "%Y_%m_%d_%H_%M");
        std::string tmstr = tmstream.str();

        std::vector<float> latency_us;
        latency_us.reserve(count);

        auto stat = test_lat_send(conn, count, size, &latency_us);
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        }

        std::ofstream lat_file("data/sr_test_lat_send_N" + std::to_string(count) + "_S" + std::to_string(size) + "_" + tmstr + ".csv");
        for (float f : latency_us){
          lat_file << f << "\n";
        }
        lat_file.close();

        auto n = count;
        std::sort (latency_us.begin(), latency_us.end());
        int q025 = (int)(n*0.025);
        int q500 = (int)(n*0.5);
        int q975 = (int)(n*0.975);
        std::cout << "## Latency Sender" << std::endl;
        std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
        std::cout << latency_us[q025] << "\t" << latency_us[q500] << "\t" << latency_us[q975] << std::endl;
      }
      conn->Close();
      return 0;
  }

    
  return 0;
}
 
