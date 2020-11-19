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
#include "buffered_read.hpp"

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


kym::Status test_lat_ping(kym::connection::BufferedReadConnection *conn, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);
  void *buf = malloc(size);
  for(int i = 0; i<count; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = conn->Send(buf, size);
    if (!send_s.ok()){
      free(buf);
      return send_s.Wrap("error sending buffer");
    }
    auto rcv_s = conn->Receive();
    auto finish = std::chrono::high_resolution_clock::now();
    if (!rcv_s.ok()){
      free(buf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    auto free_s = conn->Free(rcv_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error freeing receive buffer");
    }
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  free(buf);
  return kym::Status();

}
kym::Status test_lat_pong(kym::connection::BufferedReadConnection *conn, int count, int size){
  void *buf = malloc(size);
  // TODO(fischi) Warmup
  for(int i = 0; i<count; i++){
    auto rcv_s = conn->Receive();
    auto send_s = conn->Send(buf, size);
    if (!rcv_s.ok()){
      free(buf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    if (!send_s.ok()){
      free(buf);
      return send_s.Wrap("error sending buffer");
    }
    auto free_s = conn->Free(rcv_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error freeing receive buffer");
    }
  }
  free(buf);
  return kym::Status();

}




int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  
  std::string src = flags["source"].as<std::string>();  

  std::string filename = flags["out"].as<std::string>();  

  bool bw = flags["bw"].as<bool>();  
  bool lat = flags["lat"].as<bool>();  
  bool pingpong = flags["pingpong"].as<bool>();  

  int count = flags["iters"].as<int>();  
  int size = flags["size"].as<int>();  
  int unack = flags["unack"].as<int>();  



  bool server = flags["server"].as<bool>();  
  bool client = flags["client"].as<bool>();  

  std::vector<float> latency_m;

  std::vector<float> measurements;
  if (server){
    auto ln_s = kym::connection::ListenBufferedRead(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::BufferedReadListener *ln = ln_s.value();
    auto conn_s = ln->Accept();
    if (!conn_s.ok()){
      std::cerr << "Error accepting : " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::BufferedReadConnection *conn = conn_s.value();
    auto stat = test_lat_pong(conn, count, size);
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
      
    auto conn_s = kym::connection::DialBufferedRead(ip, 9999);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::BufferedReadConnection *conn = conn_s.value();
    auto stat = test_lat_ping(conn, count, size, &measurements);
    if (!stat.ok()){
      std::cerr << "Error running benchmark: " << stat << std::endl;
      return 1;
    }
    
        
    conn->Close();
    delete conn;
  }

  if (lat || pingpong) {
    // Handle Latency distribution
    if (!filename.empty()){
      std::ofstream file(filename);
      for (float f : measurements){
        file << f << "\n";
      }
      file.close();
    }

    if (measurements.size() > 0) {
      if (lat) {
        std::cout << "\tLatency";
      } else if(pingpong)  {
        std::cout << "\tPingPong Latency";
      }
      auto n = measurements.size();
      std::cout << std::endl;
      std::cout << "N: " << n << std::endl;
      
      std::sort (measurements.begin(), measurements.end());
      int q025 = (int)(n*0.025);
      int q500 = (int)(n*0.5);
      int q975 = (int)(n*0.975);
      std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
      std::cout << measurements[q025] << "\t" << measurements[q500] << "\t" << measurements[q975] << std::endl;
    }
  }
  return 0;
}
 
