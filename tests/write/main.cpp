#include <cstdint>
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
    opts.acknowledger = kym::connection::kAcknowledgerSend;
    auto conn_s = ln->AcceptReceiver(opts);
    if (!conn_s.ok()){
      std::cerr << "Error Accpeting  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteReceiver *rcv = conn_s.value();

    // TODO(fischi) use connection

    rcv->Close();
    delete rcv;
    ln->Close();
    delete ln;
    return 0;
  }

  if(client){
    kym::connection::WriteOpts opts;
    auto conn_s = kym::connection::DialWriteSender(ip, 9999, opts);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteSender *snd = conn_s.value();

    // TODO(fischi) use connection
    
    snd->Close();
    delete snd;
    return 0;
  }

    
  return 0;
}
 
