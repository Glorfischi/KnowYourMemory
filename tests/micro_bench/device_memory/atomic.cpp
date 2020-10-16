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
#include <vector>

#include "cxxopts.hpp"

#include "conn.hpp"
#include "endpoint.hpp"

kym::endpoint::Options opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 50,
      .max_recv_wr = 5,
      .max_send_sge = 1,
      .max_recv_sge = 1,
      .max_inline_data = 8,
    },
    .qp_type = IBV_QPT_RC,
  },
  .responder_resources = 8,
  .initiator_depth =  8,
  .retry_count = 4,  
  .rnr_retry_count = 1, 
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
      ("clients",  "Number of clients" , cxxopts::value<int>()->default_value("1"))
      ("dm", "Whether to use device memory", cxxopts::value<bool>())
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





int server(std::string ip, int clients){
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

  uint64_t init = 0;
  int ret = dm->memcpy_to_dm(dm, 0, &init, sizeof(init));
  if (ret != 0) {
    std::cerr << "Error setting device memory " << ret << std::endl;
    ibv_free_dm(dm);
    return 1;
  }

  struct ibv_mr *dm_mr = ibv_reg_dm_mr(pd, dm, 0, 512, 
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_ZERO_BASED | IBV_ACCESS_REMOTE_ATOMIC);
  if (dm_mr == nullptr) {
    std::cerr << "Error registering device memory mr " << errno << std::endl;
    perror("error");
    ibv_free_dm(dm);
    return 1;
  }



  // Setup DRAM MR
  void *ram = malloc(1024);
  if (ram == nullptr){
    std::cerr << "Error registering memory" << errno << std::endl;
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }
  *(uint64_t *)ram = 0;
  struct ibv_mr *ram_mr = ibv_reg_mr(pd, ram, 1024, 
      IBV_ACCESS_LOCAL_WRITE |IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC);
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


  std::vector<kym::endpoint::Endpoint*> eps;
  for (int i = 0; i < clients; i++){
    auto conn_s = ln->Accept(opts);
    if (!conn_s.ok()){
      std::cerr << "Error accepting connection " << conn_s.status() << std::endl;
      return 1;
    }
    eps.push_back(conn_s.value());
  }
  std::chrono::milliseconds timespan(25000); // Let's just wait 2s instead of coordinating everything...
  std::this_thread::sleep_for(timespan);
  std::cout << "server closing.." << std::endl;
  for (auto ep : eps){
    ep->Close();
  }

  // Free RAM MR
  ret = ibv_dereg_mr(ram_mr);
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

  auto stat = ln->Close();
  if(!stat.ok()){
    std::cerr << "Error closing listener " << stat << std::endl;
  }
  return 0;
}

int client(std::string ip, int n, int clients, bool use_dm){
  std::vector<std::thread> client_threads;
  for (int j = 0; j < clients; j++){
    int t = j;
    client_threads.push_back(std::thread([ip, n, t, use_dm]{
      // Client dialing
      auto conn_s = kym::endpoint::Dial(ip, 8988, opts);
      if (!conn_s.ok()){
        std::cerr << "Error dialing connection " << conn_s.status() << std::endl;
        return 1;
      }
      auto ep = conn_s.value();
      struct conn_info *ci;
      ep->GetConnectionInfo((void **)&ci);
      uint64_t addr;
      uint32_t key;
      if (use_dm) {
        addr = 0;
        key = ci->dm_key;
      } else {
        addr = ci->ram_addr;
        key = ci->ram_key;
      }
      void *buf = malloc(1024);
      ibv_mr *counter = ibv_reg_mr(ep->GetPd(), buf, 1024, IBV_ACCESS_LOCAL_WRITE);
      if (counter == nullptr){
        free(buf);
        ep->Close();
        return 1;
      }
      // prepare work request
      int batch_size = opts.qp_attr.cap.max_send_wr/2;
      ibv_sge sges[batch_size];
      ibv_send_wr wrs[batch_size];
      for (int i = 0; i < batch_size; i++){
        bool last = (i == batch_size-1);

        struct ibv_sge sge;
        sge.addr = (uintptr_t)buf;
        sge.length = sizeof(uint64_t);
        sge.lkey =  counter->lkey;
        sges[i] = sge;

        struct ibv_send_wr wr;
        wr.wr_id = i;
        wr.next = last ? NULL : &(wrs[i+1]);
        wr.sg_list = &sge;
        wr.num_sge = 1;
        wr.opcode = IBV_WR_ATOMIC_FETCH_AND_ADD;
        wr.send_flags = last ? IBV_SEND_SIGNALED : 0;  
        wr.wr.atomic.remote_addr = addr;
        wr.wr.atomic.rkey = key;
        wr.wr.atomic.compare_add = 1ULL;
        wrs[i] = wr;
      }

      struct ibv_send_wr wr = wrs[0];
      struct ibv_send_wr *bad;
      auto stat =  ep->PostSendRaw(&wr, &bad);
      if (!stat.ok()){
        std::cerr << "Error atomic send to dm " << stat << std::endl;
        ibv_dereg_mr(counter);
        free(buf);
        ep->Close();
        return 1;
      }
      auto start = std::chrono::high_resolution_clock::now();
      for (int i = 1; i<n; i++) {
        struct ibv_send_wr wr = wrs[0];
        struct ibv_send_wr *bad;
        auto stat =  ep->PostSendRaw(&wr, &bad);
        if (!stat.ok()){
          std::cerr << "Error atomic send to dm " << stat << std::endl;
          ibv_dereg_mr(counter);
          free(buf);
          ep->Close();
          return 1;
        }
        auto mr_s = ep->PollSendCq();
        if (!mr_s.ok()){
          std::cerr << "Error atomic send to ram " << mr_s.status() << std::endl;
          break;
        }
      }
      auto finish = std::chrono::high_resolution_clock::now();
      uint64_t duration = std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
      std::cout << "# Thread " << t << ": "  << batch_size*1000*1000*1000.0*(n-1)/duration << " Ops/s" << std::endl;
      ibv_dereg_mr(counter);
      free(buf);
      ep->Close();
      return 0;
    }));
  }
 
   
  for (int i = 0; i < clients; i++){ 
    client_threads[i].join();
  }
  return 0;
}

int main(int argc, char* argv[]) {
  auto flags = parse(argc,argv);
  std::string ip = flags["address"].as<std::string>();  

  int count = flags["iters"].as<int>();  
  int clients = flags["clients"].as<int>();  

  bool is_server = flags["server"].as<bool>();  
  bool is_client = flags["client"].as<bool>();  

  bool use_dm = flags["dm"].as<bool>();  
  std::cout << "#### Testing Device Memory N: " << count << "  Clients: " << clients << "DM: " << use_dm << std::endl;
  if (is_client){
    return client(ip, count, clients, use_dm);
  } else {
    return server(ip, clients);
  }
  return 0;
}
 
