
#ifndef KYM_BENCH_BENCH_HPP
#define KYM_BENCH_BENCH_HPP

#include "conn.hpp"
#include "error.hpp"

#include <vector>

kym::Status test_lat_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *lat_m);
kym::Status test_lat_recv(kym::connection::Receiver *rcv, int count, std::vector<float> *lat_m);

kym::Status test_bw_batch_send(kym::connection::BatchSender *snd, int count, int size, int batch, std::vector<float> *bw_bs);
kym::Status test_bw_batch_recv(kym::connection::Receiver *rcv, int count, int size, int batch, std::vector<float> *bw_bs);

kym::Status test_bw_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *bw_bs);
kym::Status test_bw_recv(kym::connection::Receiver *rcv, int count, int size, std::vector<float> *bw_bs);


#endif // KYM_BENCH_BENCH_HPP
