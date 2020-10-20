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
#include <vector>

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
      ("autoalloc", "Whether allocate a new receive region on every receive", cxxopts::value<bool>())
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

kym::Status read_test_lat_recv(kym::connection::ReadConnection *rcv, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);
  void *buf = calloc(1, size);

  auto reg_s = rcv->RegisterReceiveRegion(buf, size);
  if (!reg_s.ok()){
    return reg_s.status().Wrap("Error registering receive region");
  }
  kym::connection::ReceiveRegion reg = reg_s.value();

  for(int i = 0; i<count; i++){
    //std::cout << i << std::endl;
    auto start = std::chrono::high_resolution_clock::now();
    auto stat = rcv->Receive(reg);
    if (!stat.ok()){
      return stat.status().Wrap("error receiving buffer");
    }
    // std::cout << "# GOT: " << *(int *)buf_s.value().addr << std::endl;
    if (*(int *)reg.addr != i){
      return kym::Status(kym::StatusCode::Internal, "transmission error exepected " + std::to_string(i) + " got " + std::to_string(*(int *)reg.addr));
    }
    auto finish = std::chrono::high_resolution_clock::now();
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  rcv->DeregisterReceiveRegion(reg);
  free(buf);
  return kym::Status();
}
kym::Status read_test_lat_ping(kym::connection::ReadConnection *conn, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);
  void *rbuf = calloc(1, size);
  auto reg_s = conn->RegisterReceiveRegion(rbuf, size);
  if (!reg_s.ok()){
    return reg_s.status().Wrap("Error registering receive region");
  }
  kym::connection::ReceiveRegion reg = reg_s.value();

  auto buf_s = conn->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }
  // TODO(fischi) Warmup
  auto buf = buf_s.value();
  for(int i = 0; i<count; i++){
    //std::cout << i << std::endl;
    *(int *)buf.addr = i;
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = conn->Send(buf);
    if (!send_s.ok()){
      conn->Free(buf);
      conn->DeregisterReceiveRegion(reg);
      free(rbuf);
      return send_s.Wrap("error sending buffer");
    }
    auto rcv_s = conn->Receive(reg);
    auto finish = std::chrono::high_resolution_clock::now();
    if (!rcv_s.ok()){
      conn->Free(buf);
      conn->DeregisterReceiveRegion(reg);
      free(rbuf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  conn->DeregisterReceiveRegion(reg);
  free(rbuf);
  return conn->Free(buf);

}
kym::Status read_test_lat_pong(kym::connection::ReadConnection *conn, int count, int size){
  auto buf_s = conn->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }
  void *rbuf = calloc(1, size);
  auto reg_s = conn->RegisterReceiveRegion(rbuf, size);
  if (!reg_s.ok()){
    return reg_s.status().Wrap("Error registering receive region");
  }
  kym::connection::ReceiveRegion reg = reg_s.value();

  // TODO(fischi) Warmup
  auto buf = buf_s.value();
  for(int i = 0; i<count; i++){
    *(int *)buf.addr = i;
    auto rcv_s = conn->Receive(reg);
    auto send_s = conn->Send(buf);
    if (!rcv_s.ok()){
      conn->DeregisterReceiveRegion(reg);
      free(rbuf);

      conn->Free(buf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    if (!send_s.ok()){
      conn->DeregisterReceiveRegion(reg);
      free(rbuf);

      conn->Free(buf);
      return send_s.Wrap("error sending buffer");
    }
  }
  conn->DeregisterReceiveRegion(reg);
  free(rbuf);
  return conn->Free(buf);
}

kym::StatusOr<uint64_t> read_test_bw_recv(kym::connection::ReadConnection *rcv, int count, int size, int unack){
  void *rbuf = calloc(unack, size);
  auto reg_s = rcv->RegisterReceiveRegion(rbuf, unack*size);
  if (!reg_s.ok()){
    return reg_s.status().Wrap("Error registering receive region");
  }
  kym::connection::ReceiveRegion reg = reg_s.value();
  std::vector<kym::connection::ReceiveRegion> regs;
  uint64_t ids[unack];
  for (int i = 0; i<unack; i++){
    regs.push_back(kym::connection::ReceiveRegion{
      .context = (uint64_t)i,
	    .addr = (void *)((size_t)reg.addr + i*size),
	    .length = (uint32_t)size,
	    .lkey = reg.lkey
    });
  }

  for(int i = 0; i<unack; i++){
    auto id_s = rcv->ReceiveAsync(regs[i]);
    if (!id_s.ok()){
      return id_s.status().Wrap("error receiving buffer");
    }
    ids[i] = id_s.value();
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  int i = unack;
  while(i<count){
    auto stat = rcv->WaitReceive(ids[i%unack]);
    if (!stat.ok()){
      rcv->DeregisterReceiveRegion(reg);
      free(rbuf);
      return stat.Wrap("error waiting for buffer to be received");
    }
    auto id_s = rcv->ReceiveAsync(regs[i%unack]);
    if (!id_s.ok()){
      rcv->DeregisterReceiveRegion(reg);
      free(rbuf);
      return id_s.status().Wrap("error receiving buffer");
    }
    ids[i%unack] = id_s.value();
    i++;
  }
  for(int i = 0; i<unack; i++){
    auto stat = rcv->WaitReceive(ids[i]);
    if (!stat.ok()){
      rcv->DeregisterReceiveRegion(reg);
      free(rbuf);
      return stat.Wrap("error waiting for buffer to be received");
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  rcv->DeregisterReceiveRegion(reg);
  free(rbuf);
  double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  return (double)size*(double)(count-unack)/(dur/1e9);
}




int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  std::string src = flags["source"].as<std::string>();  

  std::string filename = flags["out"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  
  bool autoalloc = flags["autoalloc"].as<bool>();  

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
      if (autoalloc){
        stat = test_lat_recv(conn, count, &measurements);
      } else {
        stat = read_test_lat_recv(conn, count, size, &measurements);
      }
    } else if (bw) {
      kym::StatusOr<uint64_t> bw_s;
      if (autoalloc){
        bw_s = test_bw_recv(conn, count, size);
      } else {
        bw_s = read_test_bw_recv(conn, count, size, unack);
      }
      if (!bw_s.ok()){
        std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
        return 1;
      }
      std::cerr << "## Bandwidth Receiver (MB/s)" << std::endl;
      std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
    } else {
      if (autoalloc){
        stat = test_lat_pong(conn, conn, count, size);
      } else {
        stat = read_test_lat_pong(conn, count, size);
      }
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
      if (autoalloc){
        stat = test_lat_ping(conn, conn, count, size, &measurements);
      } else {
        stat = read_test_lat_ping(conn, count, size, &measurements);
      }
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
 
