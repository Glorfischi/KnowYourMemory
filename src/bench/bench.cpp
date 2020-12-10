#include "bench.hpp"

#include "../conn.hpp"
#include "../error.hpp"

#include "debug.h"

#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>

int set_core_affinity(int id){
  cpu_set_t set;
  CPU_ZERO(&set);
  CPU_SET(whole_cores[id%36], &set);
  return pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &set);
}

// Warmup sender. This sends slowly so not to overwhelm a cold receiver
kym::Status test_warmup_send(kym::connection::Sender *snd, int count, kym::connection::SendRegion buf){
  std::chrono::seconds sec(5);
  uint64_t nanodelay =  std::chrono::nanoseconds(sec).count() / count ;
  auto start = std::chrono::high_resolution_clock::now();
  for (int i = 0; i<count; i++){
    auto send_s = snd->Send(buf);
    if (!send_s.ok()){
      return send_s.Wrap("error sending buffer");
    }
    auto sleep_end_time =  start + std::chrono::nanoseconds(nanodelay*i);
    while (std::chrono::high_resolution_clock::now() < sleep_end_time){
        // busy loop
    }
  }
  return kym::Status();
}
// Warmup Batch sender. This sends slowly so not to overwhelm a cold receiver
kym::Status test_warmup_send(kym::connection::BatchSender *snd, int count, std::vector<kym::connection::SendRegion> bufs){
  for (int i = 0; i<count; i++){
    std::vector<kym::connection::SendRegion> buf{bufs[i%bufs.size()]};
    auto send_s = snd->Send(buf);
    if (!send_s.ok()){
      return send_s.Wrap("error sending buffer");
    }
  }
  return kym::Status();
}

// Warmup Receiver
kym::Status test_warmup_receive(kym::connection::Receiver *rcv, int count){
  for (int i = 0; i<count; i++){
    auto buf_s = rcv->Receive();
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error receiving buffer");
    }
    auto free_s = rcv->Free(buf_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error freeing receive buffer");
    }
  }
  return kym::Status();
}

kym::Status test_lat_ping(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);

  auto buf_s = snd->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }

  auto buf = buf_s.value();
  auto stat = test_warmup_send(snd, count/4, buf);
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  stat = test_warmup_receive(rcv, count/4);
  if (!stat.ok()) {
    return stat.Wrap("error during receive warmup");
  }
  for(int i = 0; i<count; i++){
    //std::cout << i << std::endl;
    *(int *)buf.addr = i;
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = snd->Send(buf);
    if (!send_s.ok()){
      snd->Free(buf);
      return send_s.Wrap("error sending buffer");
    }
    auto rcv_s = rcv->Receive();
    auto finish = std::chrono::high_resolution_clock::now();
    if (!rcv_s.ok()){
      snd->Free(buf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    auto free_s = rcv->Free(rcv_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error freeing receive buffer");
    }
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  return snd->Free(buf);

}
kym::Status test_lat_pong(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size){
  auto buf_s = snd->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }
  auto buf = buf_s.value();
  auto stat = test_warmup_receive(rcv, count/4);
  if (!stat.ok()) {
    return stat.Wrap("error during receive warmup");
  }
  stat = test_warmup_send(snd, count/4, buf);
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  for(int i = 0; i<count; i++){
    *(int *)buf.addr = i;
    auto rcv_s = rcv->Receive();
    auto send_s = snd->Send(buf);
    if (!rcv_s.ok()){
      snd->Free(buf);
      return rcv_s.status().Wrap("error receiving buffer");
    }
    if (!send_s.ok()){
      snd->Free(buf);
      return send_s.Wrap("error sending buffer");
    }
    auto free_s = rcv->Free(rcv_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error freeing receive buffer");
    }
  }
  return snd->Free(buf);

}



kym::Status test_lat_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);

  auto buf_s = snd->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }
  auto buf = buf_s.value();
  auto stat = test_warmup_send(snd, count/4, buf);
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  for(int i = 0; i<count; i++){
    //debug(stderr, "LAT SEND: %d\n",i);
    //std::cout << i << std::endl;
    *(int *)buf.addr = i;
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = snd->Send(buf);
    auto finish = std::chrono::high_resolution_clock::now();
    if (!send_s.ok()){
      snd->Free(buf);
      return send_s.Wrap("error sending buffer");
    }
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  return snd->Free(buf);
}
kym::Status test_lat_recv(kym::connection::Receiver *rcv, int count){
  auto stat = test_warmup_receive(rcv, count/4);
  if (!stat.ok()) {
    return stat.Wrap("error during receive warmup");
  }
  for(int i = 0; i<count; i++){
    if (i%500 == 0) {
      //debug(stderr, "LAT RECEIVE: %d\t[rcv: %p]\n",i, rcv);
    };
    // std::cout << i << std::endl;
    auto buf_s = rcv->Receive();
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error receiving buffer");
    }
    // std::cout << "# GOT: " << *(int *)buf_s.value().addr << std::endl;
    if (*(int *)buf_s.value().addr != i){
      return kym::Status(kym::StatusCode::Internal, "transmission error exepected " + std::to_string(i) + " got " + std::to_string(*(int *)buf_s.value().addr));
    }
    auto free_s = rcv->Free(buf_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error receiving buffer");
    }
  }
  return kym::Status();
}

kym::StatusOr<uint64_t> test_bw_batch_send(kym::connection::BatchSender *snd, int count, int size, int batch, int unack){
  if (batch > unack){
    return kym::Status(kym::StatusCode::InvalidArgument, "batch cannot be larger than unack");
  }
  if (unack > count){
    return kym::Status(kym::StatusCode::InvalidArgument, "unack cannot be larger than count");
  }
  if (unack % batch){
    std::cerr << "[INFO] falling back to unack: " << unack - (unack % batch) << std::endl;
  }
  if (count % batch){
    std::cerr << "[INFO] falling back to count: " << count - (count % batch) << std::endl;
  }
  int unack_batch = unack/batch;
  int count_batch = count/batch;
  std::vector<kym::connection::SendRegion> batches[unack_batch];
  for(int j = 0; j<unack_batch; j++){
    batches[j] = std::vector<kym::connection::SendRegion>();
    for(int i = 0; i<batch; i++){
      auto buf_s = snd->GetMemoryRegion(size);
      if (!buf_s.ok()){
        return buf_s.status().Wrap("error allocating send region");
      }
      auto buf = buf_s.value();
      *(int *)buf.addr = i;
      batches[j].push_back(buf);
    }
  }
  auto stat = test_warmup_send(snd, count/4, batches[0]); // We don't touch the other batches. This might be a problem
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  int sent = 0;
  auto start = std::chrono::high_resolution_clock::now();
  uint64_t ids[unack_batch];
  for(int i = 0; i<unack_batch; i++){
    auto send_s = snd->SendAsync(batches[i]);
    if (!send_s.ok()){
      for(auto ba : batches){
        for(auto b : ba){
          snd->Free(b);
        }
      }
      return send_s.status().Wrap("error sending buffer");
    }
    sent += batch;
    ids[i] = send_s.value();
  }

  for(int i = unack_batch; i<count_batch; i++){
    //std::cout << i << std::endl;
    auto stat = snd->Wait(ids[i%unack_batch]);
    if (!stat.ok()){
      for(auto ba : batches){
        for(auto b : ba){
          snd->Free(b);
        }
      }
      return stat.Wrap("error waiting for buffer to be sent");
    }
    auto send_s = snd->SendAsync(batches[i%unack_batch]);
    if (!send_s.ok()){
      for(auto ba : batches){
        for(auto b : ba){
          snd->Free(b);
        }
      }
      return send_s.status().Wrap("error sending buffer");
    }
    ids[i%unack_batch] = send_s.value();
  }
  auto end = std::chrono::high_resolution_clock::now();
  
  double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  return (double)size*(double)batch*count_batch/(dur/1e9);
}


kym::StatusOr<uint64_t> test_bw_send(kym::connection::Sender *snd, int count, int size, int unack){
  std::vector<kym::connection::SendRegion> bufs;
  for(int i = 0; i<unack; i++){
    auto buf_s = snd->GetMemoryRegion(size);
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error allocating send region");
    }
    auto buf = buf_s.value();
    *(int *)buf.addr = i;
    bufs.push_back(buf);
  }
  auto stat = test_warmup_send(snd, count/4, bufs[0]); // We don't touch the other bufs. That might be a problem..
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  auto start = std::chrono::high_resolution_clock::now();
  uint64_t ids[unack];
  for(int i = 0; i<unack; i++){
    // std::cout << i << std::endl;
    //std::chrono::milliseconds timespan(1); // If we start the server at the same time we will wait a little
    //std::this_thread::sleep_for(timespan);
    auto send_s = snd->SendAsync(bufs[i]);
    if (!send_s.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return send_s.status().Wrap("error sending buffer");
    }
    ids[i] = send_s.value();
  }
  for(int i = unack; i<count; i++){
    // std::cout << i << std::endl;
    auto stat = snd->Wait(ids[i%unack]);
    if (!stat.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return stat.Wrap("error waiting for buffer to be sent");
    }

    auto send_s = snd->SendAsync(bufs[i%unack]);
    if (!send_s.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return send_s.status().Wrap("error sending buffer");
    }
    ids[i%unack] = send_s.value();
  }
  auto end = std::chrono::high_resolution_clock::now();
  double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  for(int i = 0; i<unack; i++){
    snd->Wait(ids[i]);
    snd->Free(bufs[i]);
  }
  return (double)size*((double)count/(dur/1e9));
}

kym::StatusOr<uint64_t> test_bw_limit_send(kym::connection::Sender *snd, int count, int size, int unack, int max_msgps){
  std::chrono::seconds sec(1);
  uint64_t nanodelay =  std::chrono::nanoseconds(sec).count() / max_msgps ;
  std::vector<kym::connection::SendRegion> bufs;
  for(int i = 0; i<unack; i++){
    auto buf_s = snd->GetMemoryRegion(size);
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error allocating send region");
    }
    auto buf = buf_s.value();
    *(int *)buf.addr = i;
    bufs.push_back(buf);
  }
  auto stat = test_warmup_send(snd, count/4, bufs[0]); // We don't touch the other bufs. That might be a problem..
  if (!stat.ok()) {
    return stat.Wrap("error during send warmup");
  }
  auto start = std::chrono::high_resolution_clock::now();
  uint64_t ids[unack];
  for(int i = 0; i<unack; i++){
    // std::cout << i << std::endl;
    //std::chrono::milliseconds timespan(1); // If we start the server at the same time we will wait a little
    //std::this_thread::sleep_for(timespan);
    auto send_s = snd->SendAsync(bufs[i]);
    if (!send_s.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return send_s.status().Wrap("error sending buffer");
    }
    ids[i] = send_s.value();
  }
  for(int i = unack; i<count; i++){
    // std::cout << i << std::endl;
    auto stat = snd->Wait(ids[i%unack]);
    if (!stat.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return stat.Wrap("error waiting for buffer to be sent");
    }
    // TODO(Fischi) to reduce overhead we could only sleep every n send.
    auto sleep_end_time =  start + std::chrono::nanoseconds(nanodelay*i);
    while (std::chrono::high_resolution_clock::now() < sleep_end_time){
        // busy loop
    }
    auto send_s = snd->SendAsync(bufs[i%unack]);
    if (!send_s.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return send_s.status().Wrap("error sending buffer");
    }
    ids[i%unack] = send_s.value();
  }
  auto end = std::chrono::high_resolution_clock::now();
  double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(end-start).count();
  for(int i = 0; i<unack; i++){
    snd->Wait(ids[i]);
    snd->Free(bufs[i]);
  }
  return (double)size*((double)count/(dur/1e9));
}
kym::StatusOr<uint64_t> test_bw_recv(kym::connection::Receiver *rcv, int count, int size){
  int interval = std::min(count, 1000);
  std::vector<long> measurements;
  measurements.reserve(count/interval);

  auto stat = test_warmup_receive(rcv, count/4);
  if (!stat.ok()) {
    return stat.Wrap("error during receive warmup");
  }

  auto start = std::chrono::high_resolution_clock::now();
  int i = 0;
  int j = 0;
  while(i<count){
    i++;
    j++;
    if (i % 100 == 0) debug(stderr, "BW RECEIVE: %d\t[rcv: %p, core: %d]\n",i, rcv, sched_getcpu());
    auto buf_s = rcv->Receive();
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error receiving buffer");
    }
    //std::cout << "# GOT: " << *(int *)buf_s.value().addr << std::endl;
    auto free_s = rcv->Free(buf_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error receiving buffer");
    }
    if (j == interval) {
      auto now = std::chrono::high_resolution_clock::now(); 
      j = 0;
      auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(now - start).count();
      measurements.push_back(interval*(double)size/(dur/1e9));
      start = now;
    }
  }

  std::sort (measurements.begin(), measurements.end());
  int median = (int)(measurements.size()*0.5);

  //return (double)size*(double)(count-1)/(measurements[median]/1e9);
  return measurements[median];
}
