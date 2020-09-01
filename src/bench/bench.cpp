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
  return snd->Free(buf);
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

// In which interval to sample the bandwidth, measured in number of send or received messages
// Can vary a little when using batch sizes not divisible by 200
const int test_bw_sample_rate = 200;

kym::Status test_bw_batch_send(kym::connection::BatchSender *snd, int count, int size, int batch, std::vector<float> *bw_bps){
  std::vector<kym::connection::SendRegion> bufs;
  for(int i = 0; i<batch; i++){
    auto buf_s = snd->GetMemoryRegion(size);
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error allocating send region");
    }
    auto buf = buf_s.value();
    bufs.push_back(buf);
  }

  bw_bps->reserve(count/test_bw_sample_rate);
  int i = 0;
  int sent_iterval = 0;
  uint64_t data_interval = 0;

  std::chrono::high_resolution_clock::time_point finish;
  std::chrono::high_resolution_clock::time_point start;
  start = std::chrono::high_resolution_clock::now();

  while(i<count){
    std::vector<kym::connection::SendRegion> regs = bufs;
    int batch_data = batch*size;
    if (count - i < batch){
      regs = std::vector<kym::connection::SendRegion>(bufs.begin(), bufs.begin() + (count - i));
      batch_data = (count - i)*size;
    }
    i+=batch;
    auto send_s = snd->Send(bufs);
    if (!send_s.ok()){
      for(auto buf : bufs){
        snd->Free(buf);
      }
      return send_s.Wrap("error sending buffer");
    }
    sent_iterval += batch;
    data_interval += batch_data;
    if (sent_iterval >= test_bw_sample_rate || i >= count){ 
      finish = std::chrono::high_resolution_clock::now();
      double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
      start = finish;
      bw_bps->push_back(1e9*data_interval/dur);
      sent_iterval = 0;
      data_interval = 0;
    }
  }
  for(auto buf : bufs){
    snd->Free(buf);
  }
  return kym::Status();
}

kym::Status test_bw_batch_recv(kym::connection::Receiver *rcv, int count, int size, int batch, std::vector<float> *bw_bps){
  std::chrono::high_resolution_clock::time_point finish;
  std::chrono::high_resolution_clock::time_point start;
  start = std::chrono::high_resolution_clock::now();
  int i = 0;
  while(i<count){
    i++;
    auto buf_s = rcv->Receive();
    if (!buf_s.ok()){
      return buf_s.status().Wrap("error receiving buffer");
    }
    auto free_s = rcv->Free(buf_s.value());
    if (!free_s.ok()){
      return free_s.Wrap("error receiving buffer");
    }
    if (i%test_bw_sample_rate == 0 || i == count){
      finish = std::chrono::high_resolution_clock::now();
      uint64_t batch_data = (i == count) ? (i%test_bw_sample_rate)*size : test_bw_sample_rate*size;
      double dur = std::chrono::duration_cast<std::chrono::nanoseconds>(finish-start).count();
      bw_bps->push_back(1e9*batch_data/dur);
      start = finish;
    }
  }
  return kym::Status();
}

kym::Status test_bw_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *bw_bps){
  return kym::Status(kym::StatusCode::NotImplemented);
}
kym::Status test_bw_recv(kym::connection::Receiver *rcv, int count, int size, std::vector<float> *bw_bps){
  return kym::Status(kym::StatusCode::NotImplemented);
}


