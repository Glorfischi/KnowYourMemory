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

#include "write_atomic.hpp"

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

void write_atomic_test_send(std::unique_ptr<kym::connection::WriteAtomicSender> conn, int count, int thread){
    std::vector<float> latency_m;

    auto buf_s = conn->GetMemoryRegion(2*1024);
    if (!buf_s.ok()){
      std::cerr << "Error allocating send region " << buf_s.status().message() << std::endl;
      conn->Close();
      return;
    }
    auto buf = buf_s.value();
    *(uint64_t *)buf.addr = 0;

    for(int i = 0; i<count; i++){
      *(uint64_t *)buf.addr = i;
      //std::cout << "Sending " << thread << " " << i << std::endl;
      auto start = std::chrono::high_resolution_clock::now();
      auto send_s = conn->Send(buf);
      if (!send_s.ok()){
        std::cerr << "Error sending buffer " << send_s.message() << std::endl;
        conn->Free(buf);
        conn->Close();
        return;
      }
      auto finish = std::chrono::high_resolution_clock::now();
      latency_m.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
    }

    std::chrono::milliseconds timespan(1000);
    std::this_thread::sleep_for((1+thread)*timespan); // Poor mans mutex
    conn->Free(buf);
    conn->Close();

    auto n = count;
    std::sort (latency_m.begin(), latency_m.end());
    int q025 = (int)(n*0.025);
    int q500 = (int)(n*0.5);
    int q975 = (int)(n*0.975);
    std::cout << "## Thread " << thread << std::endl;
    std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
    std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;
}


void write_atomic_test_recv(std::unique_ptr<kym::connection::WriteAtomicListener> ln, int count, int threads){
  std::vector<float> latency_m;

  for (int i = 0; i<threads; i++){
    auto conn_s = ln->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error accepting " << i << " " << conn_s.message() << std::endl;
      return;
    }
  }

  for(int i = 0; i<count*threads; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto buf_s = ln->Receive();
    if (!buf_s.ok()){
      std::cerr << "Error receiving buffer " << buf_s.status().message() << std::endl;
      ln->Close();
      return;
    }
    auto buf = buf_s.value();
    // std::cout << *(uint64_t *)buf.addr << std::endl;
    auto free_s = ln->Free(buf_s.value());
    if (!free_s.ok()){
      std::cerr << free_s << std::endl;
      return;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    latency_m.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  ln->Close();
  std::chrono::milliseconds timespan(1000);
  //std::this_thread::sleep_for((1+threads)*timespan); // Poor mans mutex
  auto n = count;
  std::sort (latency_m.begin(), latency_m.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "## Server " << std::endl;
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;

}


int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  int count = flags["count"].as<int>();  


  std::cout << "#### Testing WriteDuplex Latency ####" << std::endl;
  auto ln_s = kym::connection::ListenWriteAtomic(ip, 9999);
  if (!ln_s.ok()){
    std::cerr << "Error listening " << ln_s.status().message() << std::endl;
    return 1;
  }
  auto ln = ln_s.value();

  std::thread server_thread(write_atomic_test_recv, std::move(ln), count, 7);
  std::chrono::milliseconds timespan(1000);
  std::this_thread::sleep_for(timespan);
 
  std::unique_ptr<kym::connection::WriteAtomicSender> snds[7];
  std::thread snd_threads[7];
  for (int i = 0; i < 7; i++){
    auto conn_s = kym::connection::DialWriteAtomic(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error dialing connection" << conn_s.status().message() << std::endl;
      return 1;
    }
    snds[i] = conn_s.value();
  }

  for (int i = 0; i < 7; i++){
    snd_threads[i] = std::thread(write_atomic_test_send, std::move(snds[i]), count, i);
  }
  for (int i = 0; i < 7; i++){
    snd_threads[i].join();
  }
  server_thread.join();
  
  /*auto n = count;
  std::sort (latency_m.begin(), latency_m.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << latency_m[q025] << "\t" << latency_m[q500] << "\t" << latency_m[q975] << std::endl;*/
  
  return 0;
}
 
