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

#include "write_duplex.hpp"

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("s,server", "Whether to act as a server", cxxopts::value<bool>())
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

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  bool server = flags["server"].as<bool>();  
  int count = flags["count"].as<int>();  

  std::vector<float> latency_m;

  std::cout << "#### Testing WriteDuplex Latency ####" << std::endl;
  if (server) {
    auto ln_s = kym::connection::ListenWriteDuplex(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening " << ln_s.status().message() << std::endl;
      return 1;
    }
    auto ln = ln_s.value();
    auto conn_s = ln->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error accepting " << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    for(int i = 0; i<count; i++){
      auto start = std::chrono::high_resolution_clock::now();
      auto buf_s = conn->Receive();
      if (!buf_s.ok()){
        std::cerr << "Error receiving buffer " << buf_s.status().message() << std::endl;
        conn->Close();
        ln->Close();
        return 1;
      }
      auto buf = buf_s.value();
      std::cout << *(uint64_t *)buf.addr << std::endl;
      auto free_s = conn->Free(buf_s.value());
      if (!free_s.ok()){
        std::cerr << free_s << std::endl;
        return 1;
      }
      auto finish = std::chrono::high_resolution_clock::now();
      latency_m.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
    }
    conn->Close();
    ln->Close();
  } else {
    auto conn_s = kym::connection::DialWriteDuplex(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error dialing connection" << conn_s.status().message() << std::endl;
      return 1;
    }
    auto conn = conn_s.value();

    auto buf_s = conn->GetMemoryRegion(2*1024);
    if (!buf_s.ok()){
      std::cerr << "Error allocating send region " << buf_s.status().message() << std::endl;
      conn->Close();
      return 1;
    }
    auto buf = buf_s.value();
    std::chrono::milliseconds timespan(1000); // We need to wait for the last ack to come in. o/w reciever will fail. This is hacky..
    std::this_thread::sleep_for(timespan);
    
    *(uint64_t *)buf.addr = 0;
    for(int i = 0; i<count; i++){
      *(uint64_t *)buf.addr += 1;
      std::cout << "Sending " << i << std::endl;
      auto start = std::chrono::high_resolution_clock::now();
      auto send_s = conn->Send(buf);
      if (!send_s.ok()){
        std::cerr << "Error sending buffer " << send_s.message() << std::endl;
        conn->Free(buf);
        conn->Close();
        return 1;
      }
      auto finish = std::chrono::high_resolution_clock::now();
      latency_m.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
    }

    //std::chrono::milliseconds timespan(1000); // We need to wait for the last ack to come in. o/w reciever will fail. This is hacky..
    std::this_thread::sleep_for(timespan);
    conn->Free(buf);
    conn->Close();


  } 
  auto n = count;
  std::sort (latency_m.begin(), latency_m.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;
  
  return 0;
}
 
