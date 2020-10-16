#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <ostream>
#include <string>
#include <thread>
#include <chrono>

#include <ctime>

#include <infiniband/verbs.h> 

#include "cxxopts.hpp"

#include "conn.hpp"
#include "endpoint.hpp"

kym::endpoint::Options opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 1,
      .max_recv_wr = 1,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 8,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 5,
  .initiator_depth =  5,
  .retry_count = 8,  
  .rnr_retry_count = 0, 
  .native_qp = false,
  .inline_recv = 0,
};

struct conn_info {
  uint64_t ram_addr;
  uint32_t ram_key;
  uint32_t dm_key;
};

cxxopts::ParseResult parse(int argc, char* argv[]) {
  cxxopts::Options options(argv[0], "read");
  try {
    options.add_options()
      ("client", "Whether to as client only", cxxopts::value<bool>())
      ("server", "Whether to as server only", cxxopts::value<bool>())
      ("i,address", "IP address to connect to", cxxopts::value<std::string>())
      ("n,iters",  "Number of exchanges" , cxxopts::value<int>()->default_value("1000"))
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



int server(std::string ip){
  auto ln_s = kym::endpoint::Listen(ip, 8988);
  if (!ln_s.ok()){
    std::cerr << "Error listening" << ln_s.status() << std::endl;
    return 1;
  }
  auto ln = ln_s.value();

  // Setup DM MR
  struct ibv_context *ctx = ln->GetContext();
  struct ibv_pd *pd = ln->GetPd();
  struct ibv_alloc_dm_attr attr = {};
  int len = 1024;
  attr.length = len;
  auto dm = ibv_alloc_dm(ctx, &attr);
  if (dm == nullptr) {
    std::cerr << "Error registering device memory " << errno << std::endl;
    perror("error");
    return 1;
  }
  struct ibv_mr *dm_mr = ibv_reg_dm_mr(pd, dm, 0, 512, 
      IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_ZERO_BASED);
  if (dm_mr == nullptr) {
    std::cerr << "Error registering device memory mr " << errno << std::endl;
    perror("error");
    ibv_free_dm(dm);
    return 1;
  }


  // Setup DRAM MR
  void *ram = malloc(16);
  if (ram == nullptr){
    std::cerr << "Error registering memory" << errno << std::endl;
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }
  struct ibv_mr *ram_mr = ibv_reg_mr(pd, ram, 16, IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE);
  if (ram_mr == nullptr) {
    std::cerr << "Error registering ram mr" << errno << std::endl;
    perror("error");
    free(ram);
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }

  struct conn_info ci;
  ci.dm_key = dm_mr->lkey;
  ci.ram_addr = (size_t)ram_mr->addr;
  ci.ram_key = ram_mr->lkey;

  opts.private_data = &ci;
  opts.private_data_len = sizeof(ci);

  auto conn_s = ln->Accept(opts);
  if (!conn_s.ok()){
    std::cerr << "Error accepting connection " << conn_s.status() << std::endl;
    return 1;
  }
  auto ep = conn_s.value();
  void *buf = malloc(16);
  struct ibv_mr *mr = ibv_reg_mr(ep->GetPd(), buf, 16, IBV_ACCESS_REMOTE_WRITE  | IBV_ACCESS_LOCAL_WRITE);
  auto stat = ep->PostRecv(42, mr->lkey, mr->addr, mr->length);
  if (!stat.ok()){
    std::cerr << "Error posting server mr " << stat << std::endl;
    ep->Close();
    return 1;
  }

  ep->PollRecvCq();
  ibv_dereg_mr(mr);
  free(buf);

  std::cout << "server closing.." << std::endl;
  ep->Close();

  // Free RAM MR
  int ret = ibv_dereg_mr(ram_mr);
  if (ret != 0) {
    std::cerr << "Error deregistering ram mr " << ret << std::endl;
  }
  free(ram);

  // Free DM MR
  ret = ibv_dereg_mr(dm_mr);
  if (ret != 0) {
    std::cerr << "Error deregistering dm mr " << ret << std::endl;
  }
  ret = ibv_free_dm(dm);
  if (ret != 0) {
    std::cerr << "Error freeing memory " << ret << std::endl;
  }

  stat = ln->Close();
  if(!stat.ok()){
    std::cerr << "Error closing listener " << stat << std::endl;
  }
  return 0;
}


int client(std::string ip, int count){
  auto conn_s = kym::endpoint::Dial(ip, 8988, opts);
  if (!conn_s.ok()){
    std::cerr << "Error creating ep " << conn_s.status() << std::endl;
    perror("error");
    return 1;
  }
  auto ep = conn_s.value();

  // Setup dst MR
  void *src = malloc(16);
  if (src == nullptr){
    std::cerr << "Error registering dst memory" << errno << std::endl;
    return 1;
  }
  struct ibv_mr *src_mr = ibv_reg_mr(ep->GetPd(), src, 16, IBV_ACCESS_LOCAL_WRITE);
  if (src_mr == nullptr) {
    std::cerr << "Error registering dst mr" << errno << std::endl;
    perror("error");
    free(src);
    return 1;
  }

  struct conn_info *ci;
  ep->GetConnectionInfo((void **)&ci);

  std::cout << "## Latency test ibv_read RAM" << std::endl;

  int n = count;
  std::vector<float> lat_us;
  lat_us.reserve(n);
  for (int i = 0; i<n; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto stat = ep->PostRead(i, src_mr->lkey, src_mr->addr, 16, ci->ram_addr, ci->ram_key);
    if (!stat.ok()){
      std::cerr << "Error posting read to ram " << stat << std::endl;
      break;
    }
    auto mr_s = ep->PollSendCq();
    if (!mr_s.ok()){
      std::cerr << "Error reading from ram " << mr_s.status() << std::endl;
      break;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    lat_us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  for (auto lat : lat_us){
    std::cout << lat << std::endl;
  }
  std::cout << std::endl;
  std::sort (lat_us.begin(), lat_us.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << lat_us[q025] << "\t" << lat_us[q500] << "\t" << lat_us[q975] << std::endl;
  std::cout << std::endl;


  std::cout << "## Latency test ibv_read DM" << std::endl;
  std::vector<float> dm_lat_us;
  dm_lat_us.reserve(n);
  for (int i = 0; i<n; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto stat = ep->PostRead(i, src_mr->lkey, src_mr->addr, 16, 0, ci->dm_key);
    if (!stat.ok()){
      std::cerr << "Error posting read to dm " << stat << std::endl;
      break;
    }
    auto mr_s = ep->PollSendCq();
    if (!mr_s.ok()){
      std::cerr << "Error reading from dm " << mr_s.status() << std::endl;
      break;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    dm_lat_us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  for (auto lat : dm_lat_us){
    std::cout << lat << std::endl;
  }
  std::cout << std::endl;
  std::sort (dm_lat_us.begin(), dm_lat_us.end());
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << dm_lat_us[q025] << "\t" << dm_lat_us[q500] << "\t" << dm_lat_us[q975] << std::endl;
  std::cout << std::endl;



  // Signal end to server
  auto stat = ep->PostImmidate(4, 42);
  if(!stat.ok()){
    std::cerr << "Error singaling end" << conn_s.status() << std::endl;
    return 1;
  }
  ep->Close();
  // Free src MR
  int ret = ibv_dereg_mr(src_mr);
  if (ret != 0) {
    std::cerr << "Error deregistering dst mr " << ret << std::endl;
  }
  free(src);

  return 0;
}


int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  

  int count = flags["iters"].as<int>();  

  bool is_server = flags["server"].as<bool>();  
  bool is_client = flags["client"].as<bool>();  


  std::cout << "#### Testing Device Memory Read latency N: " << count << std::endl;

  if (is_client){
    return client(ip, count);
  } else {
    return server(ip);
  }
  
  return 0;
}
 
