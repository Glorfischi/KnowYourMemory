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
  int len = 1024;
  attr.length = len;
  auto dm = ibv_alloc_dm(ctx, &attr);
  if (dm == nullptr) {
    std::cerr << "Error registering memory " << errno << std::endl;
    perror("error");
    return 1;
  }

  std::cout << "#### Copying to and from DM ####" << std::endl;
  char src[64] = "Foobar";
  char dst[64] = "Failed";

  std::cout << "     want " << src << std::endl;
  int ret = ibv_memcpy_to_dm(dm, 0, &src, 16);
  if (ret != 0) {
    std::cerr << "Error copying to DM " << ret << std::endl;
    perror("error");
    return 1;
  }
  ret = dm->memcpy_from_dm(&dst, dm, 0, 16);
  if (ret != 0) {
    std::cerr << "Error copying from DM " << ret << std::endl;
    perror("error");
    return 1;
  }
  std::cout << "     got  " << dst << std::endl;


  std::cout << "#### Testing copy speed ####" << std::endl;
  void *data = malloc(len); 
  void *buf = malloc(len); 
  void *data_dst = malloc(len); 
  auto start = std::chrono::high_resolution_clock::now();
  ret = ibv_memcpy_to_dm(dm, 0, data, len);
  if (ret != 0) {
    std::cerr << "Error copying to DM " << ret << std::endl;
    perror("error");
    return 1;
  }
  ret = dm->memcpy_from_dm(&data_dst, dm, 0, len);
  if (ret != 0) {
    std::cerr << "Error copying from DM " << ret << std::endl;
    perror("error");
    return 1;
  }
  auto finish = std::chrono::high_resolution_clock::now();
  auto lat = std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
  std::cout << "     Copying " << len << " bytes to and from DM took " << lat << " ns" << std::endl;

  start = std::chrono::high_resolution_clock::now();
  memcpy(buf, data, len);
  memcpy(data_dst, buf, len);
  finish = std::chrono::high_resolution_clock::now();
  lat = std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
  std::cout << "     Copying " << len << " bytes to and from DRAM took " << lat << " ns" << std::endl;

  free(data);

  ret = ibv_free_dm(dm);
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
 
