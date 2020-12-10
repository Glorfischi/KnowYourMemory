
#ifndef KYM_BENCH_BENCH_HPP
#define KYM_BENCH_BENCH_HPP

#include "../conn.hpp"
#include "../error.hpp"

#include <vector>


#ifndef AMD // pyhsical core mapping for the INTEL machine we are testing on
const int nr_cores = 36;
const int whole_cores[nr_cores] = 
{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53};
//{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,30,31,32,33,34,35};
#endif

int set_core_affinity(int id);
  
kym::Status test_lat_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *lat_m);
kym::Status test_lat_recv(kym::connection::Receiver *rcv, int count);

kym::Status test_lat_ping(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size, std::vector<float> *lat_m);
kym::Status test_lat_pong(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size);

kym::StatusOr<uint64_t> test_bw_send(kym::connection::Sender *snd, int count, int size, int unack);
kym::StatusOr<uint64_t> test_bw_limit_send(kym::connection::Sender *snd, int count, int size, int unack, int max_msgps);
kym::StatusOr<uint64_t> test_bw_batch_send(kym::connection::BatchSender *snd, int count, int size, int batch, int unack);
kym::StatusOr<uint64_t> test_bw_recv(kym::connection::Receiver *rcv, int count, int size);


#endif // KYM_BENCH_BENCH_HPP
