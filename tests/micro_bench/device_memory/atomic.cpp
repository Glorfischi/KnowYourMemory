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
  .responder_resources = 2,
  .initiator_depth =  3,
  .retry_count = 4,  
  .rnr_retry_count = 1, 
};




int main(int argc, char* argv[]) {
  if ( argc < 4 ){
    std::cout << "Run binary with ./atomic <address> <nr_clients> <dm | ram>" << std::endl;
    return 1;
  }
  std::cout << "#### Testing Device Memory ####" << std::endl;
  int n = 100000;
  int clients = atoi(argv[2]);
  bool use_dm = !std::strcmp("dm", argv[3]);

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

  std::cout << "## Setup done " << std::endl;

  std::thread server([ln, clients](){
      std::vector<kym::endpoint::Endpoint*> eps;
      for (int i = 0; i < clients; i++){
        auto conn_s = ln->Accept(opts);
        if (!conn_s.ok()){
          std::cerr << "Error accepting connection " << conn_s.status() << std::endl;
          return 1;
        }
        eps.push_back(conn_s.value());
      }
      std::chrono::milliseconds timespan(20000); // Let's just wait 20s instead of coordinating everything...
      std::this_thread::sleep_for(timespan);
      std::cout << "server closing.." << std::endl;
      for (auto ep : eps){
        ep->Close();
      }
      return 0;
  });

  struct ibv_mr *mr;
  if (use_dm){
    std::cout << "using device memory" << std::endl;
    mr = dm_mr;
  } else {
    std::cout << "using ram" << std::endl;
    mr = ram_mr;
  }
  std::vector<std::thread> client_threads;
  for (int j = 0; j < clients; j++){
    int t = j;
    client_threads.push_back(std::thread([argv, n, mr, t]{
      // Client dialing
      auto conn_s = kym::endpoint::Dial(argv[1], 8988, opts);
      if (!conn_s.ok()){
        std::cerr << "Error dialing connection " << conn_s.status() << std::endl;
        return 1;
      }
      auto ep = conn_s.value();
      void *buf = malloc(1024);
      ibv_mr *counter = ibv_reg_mr(ep->GetPdP(), buf, 1024, IBV_ACCESS_LOCAL_WRITE);
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
        wr.wr.atomic.remote_addr = (uint64_t)mr->addr;
        wr.wr.atomic.rkey = mr->lkey;
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
 
   
  server.join();
  for (int i = 0; i < clients; i++){ 
    client_threads[i].join();
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
 
