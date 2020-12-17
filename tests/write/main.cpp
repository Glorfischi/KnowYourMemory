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
#include "async_events.hpp"

#include "error.hpp"
#include "write.hpp"

#include "debug.h"

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
      ("c,conn",  "Number of connections" , cxxopts::value<int>()->default_value("1"))
      ("single-receiver",  "whether to have single receiver for all connections N:1" , cxxopts::value<bool>()->default_value("false"))
      ("batch",  "Number of messages to send in a single batch. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("1"))
      ("unack",  "Number of messages that can be unacknowleged. Only relevant for bandwidth benchmark", cxxopts::value<int>()->default_value("100"))
      ("sender", "Write sender type (write, writeImm, writeOff)", cxxopts::value<std::string>()->default_value("write"))
      ("ack", "Write acknowledger type (read, send)", cxxopts::value<std::string>()->default_value("write"))
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

  std::string sender = flags["sender"].as<std::string>();  
  std::string ack = flags["ack"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  
  bool pingpong = flags["pingpong"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  
  int unack = flags["unack"].as<int>();  

  int conn_count = flags["conn"].as<int>();  
  bool singlercv = flags["single-receiver"].as<bool>();  

  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  


  std::vector<std::vector<float>*> measurements(conn_count);
  std::thread ae_thread;

  kym::connection::WriteOpts opts;
  opts.raw = 0;
  opts.buffer = kym::connection::kBufferMagic;
  if (sender.compare("writeImm") == 0){
    opts.sender = kym::connection::kSenderWriteImm;
  } else if (sender.compare("writeOff") == 0){
    opts.sender = kym::connection::kSenderWriteOffset;
  } else if (sender.compare("writeRev") == 0){
    opts.sender = kym::connection::kSenderWriteReverse;
    opts.buffer = kym::connection::kBufferReverse;
  } else {
    opts.sender = kym::connection::kSenderWrite;
  }
  if (ack.compare("send") == 0){
    opts.acknowledger = kym::connection::kAcknowledgerSend;
  } else {
    opts.acknowledger = kym::connection::kAcknowledgerRead;
  }



  if (server){
    std::vector<std::thread> workers;
    std::vector<kym::connection::Receiver *> conns;

    auto ln_s = kym::connection::ListenWrite(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteListener *ln = ln_s.value();

    ae_thread = DebugTrailAsyncEvents(ln->GetListener()->GetContext());
    for (int i = 0; i<conn_count; i++){
      kym::connection::WriteConnection *conn;
      kym::connection::WriteReceiver *rcv = 0;
      kym::connection::WriteSender *snd = 0;

      if (opts.acknowledger == kym::connection::kAcknowledgerSend && opts.sender == kym::connection::kSenderWriteImm){
        auto rcv_s = ln->AcceptReceiver(opts);
        if (!rcv_s.ok()){
          std::cerr << "Error Accepting  " << rcv_s.status() << std::endl;
          return 1;
        }
        rcv = rcv_s.value();


        auto snd_s = ln->AcceptSender(opts);
        if (!snd_s.ok()){
          std::cerr << "Error Accepting  " << snd_s.status() << std::endl;
          return 1;
        }
        snd = snd_s.value();
        conn = new kym::connection::WriteConnection(snd, rcv);
      } else {
        auto conn_s = ln->AcceptConnection(opts);
        if (!conn_s.ok()){
          std::cerr << "Error Accepting  " << conn_s.status() << std::endl;
          return 1;
        }
        conn = conn_s.value();
      }
      conns.push_back(conn);

      if (!singlercv) {
        workers.push_back(std::thread([i, bw, lat, conn, snd, rcv, count, size, &measurements](){
          set_core_affinity(i+2);
          kym::Status stat;
          std::vector<float> *m = new std::vector<float>();
          if (lat) {
            stat = test_lat_recv(conn, count);
          } else if (bw) {
            auto bw_s = test_bw_recv(conn, count, size);
            if (!bw_s.ok()){
              std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
              return 1;
            }
            m->push_back(bw_s.value());
          } else {
            stat = test_lat_pong(conn, conn, count, size);
          }
          if (!stat.ok()){
            std::cerr << "Error running benchmark: " << stat << std::endl;
            return 1;
          }


          if (snd != nullptr){
            rcv->Close();
            snd->Close();
          } else {
            conn->Close();
          }
          measurements[i] = m;
          delete conn;
          return 0;
        }));
      }
    }

    if (singlercv) {
      set_core_affinity(1);
      if (bw) {
        std::vector<float> *m = new std::vector<float>();
        auto bw_s = test_bw_recv(conns, count, size);
        if (!bw_s.ok()){
          int cpu_num = sched_getcpu();
          std::cerr << "[CPU: " << cpu_num << "] Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        m->push_back(bw_s.value());
        measurements[0] = m;
      }
    }
    info(stderr, "received\n");
    for (int i = 0; i<workers.size(); i++){
      workers[i].join();
    }
    ln->Close();
    delete ln;
  }

  if(client){
    std::vector<std::thread> workers;
    std::chrono::milliseconds timespan(2000);
    std::this_thread::sleep_for(timespan);
    for (int i = 0; i<conn_count; i++){
      kym::connection::WriteConnection *conn;
      kym::connection::WriteReceiver *rcv = 0;
      kym::connection::WriteSender *snd = 0;

      if (opts.acknowledger == kym::connection::kAcknowledgerSend && opts.sender == kym::connection::kSenderWriteImm){
        auto snd_s = kym::connection::DialWriteSender(ip, 9999, opts);
        if (!snd_s.ok()){
          std::cerr << "Error Dialing  " << snd_s.status() << std::endl;
          return 1;
        }
        snd = snd_s.value();
        auto rcv_s = kym::connection::DialWriteReceiver(ip, 9999, opts);
        if (!rcv_s.ok()){
          std::cerr << "Error Dialing  " << rcv_s.status() << std::endl;
          return 1;
        }
        rcv = rcv_s.value();

        conn = new kym::connection::WriteConnection(snd, rcv);
      } else {
        auto conn_s = kym::connection::DialWriteConnection(ip, 9999, opts);
        if (!conn_s.ok()){
          std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
          return 1;
        }
        conn = conn_s.value();
      }
      ae_thread = DebugTrailAsyncEvents(conn->GetEp()->GetContext());
      workers.push_back(std::thread([i, bw, lat, conn, snd, rcv, count, batch, size, unack, &measurements](){
        set_core_affinity(i+2);
        std::chrono::milliseconds timespan(1000);
        std::this_thread::sleep_for(timespan);
        kym::Status stat;
        std::vector<float> *m = new std::vector<float>();
        if (lat) {
          stat = test_lat_send(conn, count, size, m);
        } else if (bw) {
          if (batch > 1){
            auto bw_s = test_bw_batch_send(conn, count, size, batch, unack);
            if (!bw_s.ok()){
              std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
              return 1;
            }
            m->push_back(bw_s.value());
          } else {
            auto bw_s = test_bw_send(conn, count, size, unack);
            if (!bw_s.ok()){
              std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
              return 1;
            }
            m->push_back(bw_s.value());
          }
        } else {
          stat = test_lat_ping(conn, conn, count, size, m);
        }
        if (!stat.ok()){
          std::cerr << "Error running benchmark: " << stat << std::endl;
          return 1;
        } 
        if (snd != nullptr){
          snd->Close();
          rcv->Close();
        } else {
          conn->Close();
        }
        measurements[i] = m;
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
      file << conn_count <<  " " << size << " " << unack << " " << batch << " " <<  bandwidth << "\n";
      file.close();
    }
  }
  
  return 0;
}
 
