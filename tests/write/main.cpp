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

#include "write.hpp"

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "sendrecv");
  try {
    options.add_options()
      ("bw", "Whether to test bandwidth", cxxopts::value<bool>())
      ("lat", "Whether to test latency", cxxopts::value<bool>())
      ("client", "Whether to as client only", cxxopts::value<bool>())
      ("server", "Whether to as server only", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("source", "source IP address", cxxopts::value<std::string>()->default_value(""))
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
      ("s,size",  "Size of message to exchange", cxxopts::value<int>()->default_value("60"))
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

  std::vector<float> latency_m;

  std::cout << "#### Testing WriteConnection ####" << std::endl;

  if (server){
    auto ln_s = kym::connection::ListenWrite(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteListener *ln = ln_s.value();

    kym::connection::WriteOpts opts;
    opts.raw = 0;
    opts.sender = kym::connection::kSenderWriteOffset;
    opts.acknowledger = kym::connection::kAcknowledgerRead;
    opts.buffer = kym::connection::kBufferMagic;
    auto conn_s = ln->AcceptReceiver(opts);
    if (!conn_s.ok()){
      std::cerr << "Error Accepting  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteReceiver *rcv = conn_s.value();

    std::vector<float> latency_us;
    auto stat = test_lat_recv(rcv, count, &latency_us);
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    }

    rcv->Close();
    delete rcv;
    ln->Close();
    delete ln;
    return 0;
  }

  if(client){
    kym::connection::WriteOpts opts;
    opts.raw = 0;
    opts.sender = kym::connection::kSenderWriteOffset;
    opts.acknowledger = kym::connection::kAcknowledgerRead;
    opts.buffer = kym::connection::kBufferMagic;
    auto conn_s = kym::connection::DialWriteSender(ip, 9999, opts);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteSender *snd = conn_s.value();
    std::chrono::milliseconds timespan(1000); // Make sure we received all acks
    std::this_thread::sleep_for(timespan);
    std::vector<float> latency_us;
    auto stat = test_lat_send(snd, count, size, &latency_us);
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    }
    std::this_thread::sleep_for(timespan);
    snd->Close();
    delete snd;
    auto n = count;
    std::sort (latency_us.begin(), latency_us.end());
    int q025 = (int)(n*0.025);
    int q500 = (int)(n*0.5);
    int q975 = (int)(n*0.975);
    std::cout << "## Latency Sender" << std::endl;
    std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
    std::cout << latency_us[q025] << "\t" << latency_us[q500] << "\t" << latency_us[q975] << std::endl;

    return 0;
  }

    
  return 0;
}
 
