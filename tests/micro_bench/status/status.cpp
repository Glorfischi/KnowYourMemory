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


#include "error.hpp"

void BaseFunc(long *i){
  __asm__ __volatile__(""); // should  prevent the compiler from optimizing away to loop
  *i = *i+1;
}

int IntegerFunc(long *i){
  BaseFunc(i);
  return 0;
}

int IntegerFunc(long *i, long *res){
  BaseFunc(i);
  *res = *i%5;
  return 0;
}

kym::Status StatusFunc(long *i){
  BaseFunc(i);
  return kym::Status();
}

kym::StatusOr<long> StatusOrFunc(long *i){
  BaseFunc(i);
  return *i%5;
}


int main(int argc, char* argv[]) {
  std::cout << "#### Testing impact of returning Status ####" << std::endl;
  long n = 100000000;

  long i = 0;
  auto start = std::chrono::high_resolution_clock::now();
  while (i < n){
    BaseFunc(&i);
  }
  auto end = std::chrono::high_resolution_clock::now();
  i = 0;
  start = std::chrono::high_resolution_clock::now();
  while (i < n){
    int a = IntegerFunc(&i);
    if (a){
      std::cerr << "ERROR " << a << std::endl;
      exit(1);
    }
  }
  end = std::chrono::high_resolution_clock::now();
  double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  std::cout << "IntegerFunc:\t\t" << dur << " ns\tN: " << n << std::endl;
  std::cout << "IntegerFunc:\t\t" << dur/n << " ns\tN: " << 1 << std::endl;


  i = 0;
  start = std::chrono::high_resolution_clock::now();
  while (i < n){
    kym::Status a = StatusFunc(&i);
    if (!a.ok()){
      std::cerr << "ERROR " << a << std::endl;
      exit(1);
    }
  }
  end = std::chrono::high_resolution_clock::now();
  dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  std::cout << "StatusFunc:\t\t" << dur << " ns\tN: " << n << std::endl;
  std::cout << "StatusFunc:\t\t" << dur/n << " ns\tN: " << 1 << std::endl;


  std::cout << "#### Testing impact of returning StatusOr ####" << std::endl;
  i = 0;
  start = std::chrono::high_resolution_clock::now();
  while (i < n){
    long b;
    long a = IntegerFunc(&i, &b);
    if (a){
      std::cerr << "ERROR " << a << std::endl;
      exit(1);
    }
    b++;
  }
  end = std::chrono::high_resolution_clock::now();
  dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  std::cout << "IntegerFunc \\w res:\t" << dur << " ns\tN: " << n << std::endl;
  std::cout << "IntegerFunc \\w res:\t" << dur/n << " ns\tN: " << 1 << std::endl;

  i = 0;
  start = std::chrono::high_resolution_clock::now();
  while (i < n){
    auto a = StatusOrFunc(&i);
    if (!a.ok()){
      std::cerr << "ERROR " << a.status() << std::endl;
      exit(1);
    }
    long b = a.value()+1;
  }
  end = std::chrono::high_resolution_clock::now();
  dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  std::cout << "StatusOrFunc:\t\t" << dur << " ns\tN: " << n << std::endl;
  std::cout << "StatusOrFunc:\t\t" << dur/n << " ns\tN: " << 1 << std::endl;
  return 0;

}
 
