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
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
     ;
 
    auto result = options.parse(argc, argv);
    if (!result.count("address")) {
      std::cerr << "Specify an address" << std::endl;
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


int test_write_integrity(std::string ip){
  std::cout << "#### Testing WriteConnection ####" << std::endl;
  auto server = std::thread([ip]{
    auto ln_s = kym::connection::ListenWrite(ip, 9999);
    if (!ln_s.ok()){
      std::cerr << "Error listening  " << ln_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteListener *ln = ln_s.value();

    kym::connection::WriteOpts opts;
    opts.raw = 0;
    opts.sender = kym::connection::kSenderWrite;
    opts.acknowledger = kym::connection::kAcknowledgerRead;
    opts.buffer = kym::connection::kBufferMagic;
    auto conn_s = ln->AcceptReceiver(opts);
    if (!conn_s.ok()){
      std::cerr << "Error Accepting  " << conn_s.status() << std::endl;
      return 1;
    }
    auto rcv = conn_s.value();

    auto buf_s = rcv->Receive();
    auto buf = buf_s.value();
    if (((char *)buf.addr)[127*1024*1024-1] != 'Q'){
      std::cerr << "ERROR: buffer was not received correctly" << std::endl; 
    }
    if (!buf_s.ok()){
      std::cerr << "ERROR: " << buf_s.status() << std::endl; 
    }
    std::chrono::milliseconds timespan(1000); // Make sure receiver is ready
    std::this_thread::sleep_for(timespan);

    rcv->Close();
    delete rcv;
    ln->Close();
    delete ln;
    return 0;
  });

  auto client = std::thread([ip]{
    std::chrono::milliseconds timespan(1000); // Make sure receiver is ready
    kym::connection::WriteOpts opts;
    opts.raw = 0;
    opts.sender = kym::connection::kSenderWrite;
    opts.acknowledger = kym::connection::kAcknowledgerRead;
    opts.buffer = kym::connection::kBufferMagic;
    std::this_thread::sleep_for(timespan);
    auto conn_s = kym::connection::DialWriteSender(ip, 9999, opts);
    if (!conn_s.ok()){
      std::cerr << "Error Dialing  " << conn_s.status() << std::endl;
      return 1;
    }
    kym::connection::WriteSender *snd = conn_s.value();
    auto buf_s = snd->GetMemoryRegion(127*1024*1024);
    if (!buf_s.ok()) {
      std::cerr << "ERROR: " << buf_s.status() << std::endl; 
      return 1;
    }
    auto buf = buf_s.value();
    ((char *)buf.addr)[127*1024*1024-1] = 'Q';
    std::this_thread::sleep_for(timespan);
    auto stat = snd->Send(buf);
    if (!stat.ok()){
      std::cerr << "error sending: " << stat << std::endl; 
    }
    snd->Close();
    delete snd;
    return 0;
  });
  
  server.join();
  client.join();
  return 0;
}


int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  

  test_write_integrity(ip);
  return 0;
}
 
