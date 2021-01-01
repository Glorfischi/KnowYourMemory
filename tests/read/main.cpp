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

#include "debug.h"

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
      ("fence", "Whether to read the ack at the same time using memory fences", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("source", "source IP address", cxxopts::value<std::string>()->default_value(""))
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("60"))
      ("unack",  "Number of messages that can be unacknowleged. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("100"))
      ("out", "filename to output measurements", cxxopts::value<std::string>()->default_value(""))
      ("autoalloc", "Whether allocate a new receive region on every receive", cxxopts::value<bool>())
      ("c,conn",  "Number of connections" , cxxopts::value<int>()->default_value("1"))
      ("single-receiver",  "whether to have single receiver for all connections N:1" , cxxopts::value<bool>()->default_value("false"))
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
  for(int i = 0; i<count/4; i++){
    //std::cout << i << std::endl;
    auto stat = rcv->Receive(reg);
    if (!stat.ok()){
      return stat.status().Wrap("error receiving buffer");
    }
  }

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

  for(int i = 0; i<count/16; i++){
    //std::cout << i << std::endl;
    auto stat = rcv->Receive(regs[i%unack]);
    if (!stat.ok()){
      return stat.status().Wrap("error receiving buffer");
    }
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
    if (*(int *)regs[i%unack].addr != i-unack+1) info(stderr, "ERROR: expected %d, got %d\n", i-unack+1, *(int *)regs[i%unack].addr);
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


kym::StatusOr<uint64_t> read_test_bw_recv(std::vector<kym::connection::ReadConnection *>rcvs, int count, int size, int unack){
  int n = rcvs.size();
  void *rbuf = calloc(unack, size);
  // We use a single receiver for all
  auto reg_s = rcvs[0]->RegisterReceiveRegion(rbuf, unack*size);
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

  for(int i = 0; i<count/16; i++){
    //std::cout << i << std::endl;
    auto stat = rcvs[i%n]->Receive(regs[i%unack]);
    if (!stat.ok()){
      return stat.status().Wrap("error receiving buffer");
    }
  }

  for(int i = 0; i<unack; i++){
    auto id_s = rcvs[i%n]->ReceiveAsync(regs[i]);
    if (!id_s.ok()){
      return id_s.status().Wrap("error receiving buffer");
    }
    ids[i] = id_s.value();
  }
  
  auto start = std::chrono::high_resolution_clock::now();
  int i = unack;
  while(i<count){
    auto stat = rcvs[i%n]->WaitReceive(ids[i%unack]);
    if (!stat.ok()){
      rcvs[i%n]->DeregisterReceiveRegion(reg);
      free(rbuf);
      return stat.Wrap("error waiting for buffer to be received");
    }
    auto id_s = rcvs[i%n]->ReceiveAsync(regs[i%unack]);
    if (!id_s.ok()){
      rcvs[0]->DeregisterReceiveRegion(reg);
      free(rbuf);
      return id_s.status().Wrap("error receiving buffer");
    }
    ids[i%unack] = id_s.value();
    i++;
  }
  for(int i = 0; i<unack; i++){
    auto stat = rcvs[i%n]->WaitReceive(ids[i]);
    if (!stat.ok()){
      rcvs[0]->DeregisterReceiveRegion(reg);
      free(rbuf);
      return stat.Wrap("error waiting for buffer to be received");
    }
  }
  auto end = std::chrono::high_resolution_clock::now();
  rcvs[0]->DeregisterReceiveRegion(reg);
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
  bool pingpong = flags["pingpong"].as<bool>();  

  bool fence = flags["fence"].as<bool>();  
  bool autoalloc = flags["autoalloc"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int unack = flags["unack"].as<int>();  

  int conn_count = flags["conn"].as<int>();  
  bool singlercv = flags["single-receiver"].as<bool>(); 

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  


  std::vector<std::vector<float>*> measurements(conn_count);
  if (server){
    std::vector<std::thread> workers;
    std::vector<kym::connection::ReadConnection*> conns;

    auto ln_s = kym::connection::ListenRead(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::ReadListener *ln = ln_s.value();
    for (int i = 0; i<conn_count; i++){
      auto conn_s = ln->Accept(fence);
      if (!conn_s.ok()){
        std::cerr << "Error accepting " << conn_s.status().message() << std::endl;
        return 1;
      }
      auto conn = conn_s.value();
      conns.push_back(conn);
    }

    if (!singlercv) {
      for (int i = 0; i<conn_count; i++){
        auto conn = conns[i];
        workers.push_back(std::thread([i, lat, bw, conn, count, unack, size, &measurements](){
          set_core_affinity(i+2);
          std::vector<float> *m = new std::vector<float>();
          kym::Status stat;
          if (lat) {
            stat = read_test_lat_recv(conn, count, size, m);
          } else if (bw) {
            kym::StatusOr<uint64_t> bw_s;
            bw_s = read_test_bw_recv(conn, count, size, unack);
            if (!bw_s.ok()){
              std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
              return 1;
            }
            m->push_back(bw_s.value());
          } else {
            stat = read_test_lat_pong(conn, count, size);
          }
          if (!stat.ok()){
            std::cerr << "Error running benchmark: " << stat << std::endl;
            return 1;
          }
          measurements[i] = m;
          conn->Close();
          delete conn;
          return 0;
        }));
      }

      for (int i = 0; i<workers.size(); i++){
        workers[i].join();
      }

    } else {
      set_core_affinity(0);
      std::vector<float> *m = new std::vector<float>();
      kym::StatusOr<uint64_t> bw_s;
      bw_s = read_test_bw_recv(conns, conn_count*count, size, conn_count*unack);
      if (!bw_s.ok()){
        std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
        return 1;
      }
      m->push_back(bw_s.value());
      measurements[0] = m;
      for (auto conn : conns) {
        conn->Close();
        delete conn;
      }

    }
    ln->Close();
    delete ln;
  }

  if(client){
    std::chrono::milliseconds timespan(1000); // Make sure we received all acks
    std::this_thread::sleep_for(timespan);
    std::vector<std::thread> workers;
    std::vector<kym::connection::ReadConnection*> conns;

    for (int i = 0; i<conn_count; i++){
      auto conn_s = kym::connection::DialRead(ip, 9999, fence);
      if (!conn_s.ok()){
        std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
        return 1;
      }
      kym::connection::ReadConnection *conn = conn_s.value();
      conns.push_back(conn);
    }

    std::this_thread::sleep_for(timespan);
    for (int i = 0; i<conn_count; i++){
      auto conn = conns[i];
      workers.push_back(std::thread([i, bw, lat, conn, count, size, unack, &measurements](){
        set_core_affinity(i+2);
        std::vector<float> *m = new std::vector<float>();

        kym::Status stat;
        if (lat) {
          stat = test_lat_send(conn, count, size, m);
        } else if (bw) {
          auto bw_s = test_bw_send(conn, count, size, unack);
          if (!bw_s.ok()){
            std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
            return 1;
          }
          m->push_back(bw_s.value());
        } else {
          stat = read_test_lat_ping(conn, count, size, m);
        }
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        } 
        std::chrono::milliseconds timespan(1000); // Make sure we received all acks
        std::this_thread::sleep_for(timespan);
        measurements[i] = m;
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
      file << conn_count <<  " " << size << " " << unack << " " << 0 << " " <<  bandwidth << "\n";
      file.close();
    }
  }
  
  return 0;
}
 
