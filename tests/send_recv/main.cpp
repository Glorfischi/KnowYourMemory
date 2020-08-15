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




#include "cxxopts.hpp"

#include "conn.hpp"

#include "send_receive.hpp"

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("srq", "Whether to use a shard receive queue", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("n,count", "How many times to repeat measurement", cxxopts::value<int>()->default_value("1000"))
     ;
 
    auto result = options.parse(argc, argv);
    if (!result.count("address")) {
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


void sr_test_lat_send(std::unique_ptr<kym::connection::Sender> conn, int count, std::vector<float> *latency_m){
  ;
  std::chrono::milliseconds timespan(1000); // This is because of a race condition...
  std::this_thread::sleep_for(timespan);

  auto buf_s = conn->GetMemoryRegion(2048);
  if (!buf_s.ok()){
    std::cerr << "Error allocating send region " << buf_s.status().message() << std::endl;
    return;
  }
  auto buf = buf_s.value();
  for(int i = 0; i<count; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = conn->Send(buf);
    if (!send_s.ok()){
      std::cerr << "Error sending buffer " << send_s.message() << std::endl;
      conn->Free(buf);
      return;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    latency_m->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  conn->Free(buf);

  std::this_thread::sleep_for(timespan);

  }

void sr_test_lat_recv(std::unique_ptr<kym::connection::Receiver> conn, int count){
  std::vector<float> latency_m;
  latency_m.reserve(count);

  for(int i = 0; i<count; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto buf_s = conn->Receive();
    if (!buf_s.ok()){
      std::cerr << "Error receiving buffer " << buf_s.status().message() << std::endl;
      return;
    }
    auto free_s = conn->Free(buf_s.value());
    auto finish = std::chrono::high_resolution_clock::now();
    latency_m.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }

  auto n = count;
  std::sort (latency_m.begin(), latency_m.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "## Latency Receiver" << std::endl;
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;
}

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  bool srq = flags["srq"].as<bool>();  
  int count = flags["count"].as<int>();  

  std::vector<float> latency_m;

  std::cout << "#### Testing SendReceive Latency ####" << std::endl;
  std::thread server_thread( [srq, ip, count] {
    std::unique_ptr<kym::connection::SendReceiveConnection> conn;
    std::unique_ptr<kym::connection::SendReceiveListener> ln;
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

    sr_test_lat_recv(std::move(conn), count);
    return 0;
  });

  std::thread client_thread( [ip, count] {
    auto conn_s = kym::connection::DialSendReceive(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error dialing send_receive co_nection" << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    // Create a cpu_set_t object representing a set of CPUs. Clear it and mark only CPU i as set.
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    int rc = pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
    if (rc != 0) {
      std::cerr << "Error calling pthread_setaffinity_np: " << rc << "\n";
    }

    std::ofstream lat_file("data/sr_test_lat_send-" + std::to_string(count) + ".csv");
    std::vector<float> latency_m;
    latency_m.reserve(count);
    sr_test_lat_send(std::move(conn), count, &latency_m);
    for (float f : latency_m){
      lat_file << f << "\n";
    }
    lat_file.close();

    auto n = count;
    std::sort (latency_m.begin(), latency_m.end());
    int q025 = (int)(n*0.025);
    int q500 = (int)(n*0.5);
    int q975 = (int)(n*0.975);
    std::cout << "## Latency Sender" << std::endl;
    std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
    std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;
    return 0;
  });

  client_thread.join();
  server_thread.join();
    
  return 0;
}
 
