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

#include "conn.hpp"
#include "endpoint.hpp"

kym::endpoint::Options opts = {
  .qp_attr = {
    .cap = {
      .max_send_wr = 5,
      .max_recv_wr = 5,
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
};



int main(int argc, char* argv[]) {
  std::cout << "#### Testing  Device Memory ####" << std::endl;

  auto ln_s = kym::endpoint::Listen(argv[1], 8988);
  if (!ln_s.ok()){
    std::cerr << "Error listening" << ln_s.status() << std::endl;
    return 1;
  }
  auto ln = ln_s.value();

  // Setup DM MR
  struct ibv_context *ctx = ln->GetContext();
  struct ibv_pd *pd = ln->GetPdP();
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
      IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_ZERO_BASED);
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
  struct ibv_mr *ram_mr = ibv_reg_mr(pd, ram, 1024, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
  if (ram_mr == nullptr) {
    std::cerr << "Error registering ram mr" << errno << std::endl;
    perror("error");
    free(ram);
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }

  // Setup src MR
  void *src = malloc(1024);
  if (src == nullptr){
    std::cerr << "Error registering src memory" << errno << std::endl;
    ibv_dereg_mr(ram_mr);
    free(ram);
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }
  struct ibv_mr *src_mr = ibv_reg_mr(pd, src, 1024, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
  if (ram_mr == nullptr) {
    std::cerr << "Error registering src mr" << errno << std::endl;
    perror("error");
    free(src);
    ibv_dereg_mr(ram_mr);
    free(ram);
    ibv_dereg_mr(dm_mr);
    ibv_free_dm(dm);
    return 1;
  }

  std::cout << "## Setup done " << std::endl;
  std::thread server([ln](){
      auto conn_s = ln->Accept(opts);
      if (!conn_s.ok()){
        std::cerr << "Error accepting connection " << conn_s.status() << std::endl;
        return 1;
      }
      auto ep = conn_s.value();
      void *buf = malloc(16);
      struct ibv_mr *mr = ibv_reg_mr(ep->GetPdP(), buf, 16, IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_LOCAL_WRITE);
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
      return 0;
  });

  // Client dialing
  auto conn_s = kym::endpoint::Dial(argv[1], 8988, opts);
  if (!conn_s.ok()){
    std::cerr << "Error dialing connection " << conn_s.status() << std::endl;
    perror("error");
    server.join();
    return 1;
  }
  auto ep = conn_s.value();
  std::cout << "Connected" << std::endl;


  std::cout << "## Latency test ibv_write RAM" << std::endl;

  int n = 1000;
  std::vector<float> lat_us;
  lat_us.reserve(n);
  for (int i = 0; i<n; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto stat = ep->PostWrite(i, src_mr->lkey, src_mr->addr, 16, (uint64_t)ram_mr->addr, ram_mr->lkey);
    if (!stat.ok()){
      std::cerr << "Error posting write to ram " << stat << std::endl;
      break;
    }
    auto mr_s = ep->PollSendCq();
    if (!mr_s.ok()){
      std::cerr << "Error writing to ram " << mr_s.status() << std::endl;
      break;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    lat_us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  std::sort (lat_us.begin(), lat_us.end());
  int q025 = (int)(n*0.025);
  int q500 = (int)(n*0.5);
  int q975 = (int)(n*0.975);
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << lat_us[q025] << "\t" << lat_us[q500] << "\t" << lat_us[q975] << std::endl;


  std::cout << "## Latency test ibv_write DM" << std::endl;
  std::vector<float> dm_lat_us;
  dm_lat_us.reserve(n);
  for (int i = 0; i<n; i++) {
    auto start = std::chrono::high_resolution_clock::now();
    auto stat = ep->PostWrite(i, src_mr->lkey, src_mr->addr, 16, 0, dm_mr->lkey);
    if (!stat.ok()){
      std::cerr << "Error posting write to dm " << stat << std::endl;
      break;
    }
    auto mr_s = ep->PollSendCq();
    if (!mr_s.ok()){
      std::cerr << "Error writing to dm " << mr_s.status() << std::endl;
      break;
    }
    auto finish = std::chrono::high_resolution_clock::now();
    dm_lat_us.push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  std::sort (dm_lat_us.begin(), dm_lat_us.end());
  std::cout << "q025" << "\t" << "q50" << "\t" << "q975" << std::endl;
  std::cout << dm_lat_us[q025] << "\t" << dm_lat_us[q500] << "\t" << dm_lat_us[q975] << std::endl;

  // Signal end to server
  auto stat = ep->PostImmidate(4, 42);
  if(!stat.ok()){
    std::cerr << "Error singaling end" << conn_s.status() << std::endl;
    server.join();
    return 1;
  }
  ep->Close();
  server.join();

  // Free src MR
  int ret = ibv_dereg_mr(src_mr);
  if (ret != 0) {
    std::cerr << "Error deregistering src mr " << ret << std::endl;
  }
  free(src);

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

  stat = ln->Close();
  if(!stat.ok()){
    std::cerr << "Error closing listener " << stat << std::endl;
  }

  return 0;
}
 
