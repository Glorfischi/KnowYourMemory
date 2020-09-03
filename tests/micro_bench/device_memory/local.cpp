#include <cstdint>
#include <cerrno>
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

#include <infiniband/verbs.h> 

#include "conn.hpp"
#include "endpoint.hpp"




int main(int argc, char* argv[]) {
  std::cout << "#### Testing  Device Memory ####" << std::endl;

  auto ln_s = kym::endpoint::Listen(argv[1], 8988);
  if (!ln_s.ok()){
    std::cerr << "Error listening" << ln_s.status() << std::endl;
    return 1;
  }
  auto ln = ln_s.value();

  struct ibv_context *ctx = ln->GetContext();
  struct ibv_alloc_dm_attr attr = {};
  attr.length = 64;
  auto dm = ibv_alloc_dm(ctx, &attr);
  if (dm == nullptr) {
    std::cerr << "Error registering memory " << errno << std::endl;
    perror("error");
    return 1;
  }
  int ret = ibv_free_dm(dm);
  if (ret != 0) {
    std::cerr << "Error freeing memory " << ret << std::endl;
    perror("error");
    return 1;
  }

  auto stat = ln->Close();
  if(!stat.ok()){
    std::cerr << "Error closing listener " << stat << std::endl;
    return 1;
  }

  return 0;
}
 
