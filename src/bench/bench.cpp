#include "bench.hpp"

#include "conn.hpp"
#include "error.hpp"

#include <vector>
#include <chrono>

kym::Status test_lat_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *lat_us){
  lat_us->reserve(count);

  auto buf_s = snd->GetMemoryRegion(size);
  if (!buf_s.ok()){
    return buf_s.status().Wrap("error allocating send region");
  }
  auto buf = buf_s.value();

  for(int i = 0; i<count; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto send_s = snd->Send(buf);
    if (!send_s.ok()){
      snd->Free(buf);
      return send_s.Wrap("error sending buffer");
    }
    auto finish = std::chrono::high_resolution_clock::now();
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  snd->Free(buf);
  return kym::Status();
}
kym::Status test_lat_recv(kym::connection::Receiver *rcv, int count, std::vector<float> *lat_us){
  lat_us->reserve(count);

  for(int i = 0; i<count; i++){
    auto start = std::chrono::high_resolution_clock::now();
    auto buf_s = rcv->Receive();
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error receiving buffer");
    }
    auto free_s = rcv->Free(buf_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error receiving buffer");
    }
    auto finish = std::chrono::high_resolution_clock::now();
    lat_us->push_back(std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count()/1000.0);
  }
  return kym::Status();
}

kym::Status test_bw_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *bw_bps){
  return kym::Status(kym::StatusCode::NotImplemented);
}
kym::Status test_bw_recv(kym::connection::Receiver *rcv, int count, int size, std::vector<float> *bw_bps){
  return kym::Status(kym::StatusCode::NotImplemented);
}


