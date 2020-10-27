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
#include "write.hpp"

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

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int batch = flags["batch"].as<int>();  
  int unack = flags["unack"].as<int>();  


  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> latency_m;

  std::cout << "#### Testing WriteConnection ####" << std::endl;
  kym::connection::WriteOpts opts;
  opts.raw = 0;
  if (sender.compare("writeImm") == 0){
    opts.sender = kym::connection::kSenderWriteImm;
  } else if (sender.compare("writeOff") == 0){
    opts.sender = kym::connection::kSenderWriteOffset;
  } else {
    opts.sender = kym::connection::kSenderWrite;
  }
  if (ack.compare("send") == 0){
    opts.acknowledger = kym::connection::kAcknowledgerSend;
  } else {
    opts.acknowledger = kym::connection::kAcknowledgerRead;
  }
  opts.buffer = kym::connection::kBufferReverse;



  std::vector<float> measurements;
  if (server){
    auto ln_s = kym::connection::ListenWrite(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteListener *ln = ln_s.value();
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


    if (snd != nullptr){
      rcv->Close();
      snd->Close();
    } else {
      conn->Close();
    }
    delete conn;
    ln->Close();
    delete ln;
  }

  if(client){
    std::chrono::milliseconds timespan(1000); // Make sure we received all acks
    std::this_thread::sleep_for(timespan);
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
    std::this_thread::sleep_for(timespan);
    kym::Status stat;
    if (lat) {
      stat = test_lat_send(conn, count, size, &measurements);
      std::cout << "## Latency Sender" << std::endl;
    } else if (bw) {
      if (batch > 1){
        auto bw_s = test_bw_batch_send(conn, count, size, batch, unack);
        if (!bw_s.ok()){
          std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        std::cerr << "## Bandwidth Sender (MB/s)" << std::endl;
        std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
      } else {
        auto bw_s = test_bw_send(conn, count, size, unack);
        if (!bw_s.ok()){
          std::cerr << "Error running benchmark: " << bw_s.status() << std::endl;
          return 1;
        }
        std::cerr << "## Bandwidth Sender (MB/s)" << std::endl;
        std::cout << (double)bw_s.value()/(1024*1024) << std::endl;
      }
    } else {
      stat = test_lat_ping(conn, conn, count, size, &measurements);
      std::cout << "## Pingpong Sender" << std::endl;
    }
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    } 
    std::this_thread::sleep_for(timespan);
    if (snd != nullptr){
      snd->Close();
      rcv->Close();
    } else {
      conn->Close();
    }
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
 
