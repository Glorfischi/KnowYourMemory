
#ifndef KYM_BENCH_BENCH_HPP
#define KYM_BENCH_BENCH_HPP

#include "../conn.hpp"
#include "../error.hpp"

#include <vector>

kym::Status test_lat_send(kym::connection::Sender *snd, int count, int size, std::vector<float> *lat_m);
kym::Status test_lat_recv(kym::connection::Receiver *rcv, int count);

kym::Status test_lat_ping(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size, std::vector<float> *lat_m);
kym::Status test_lat_pong(kym::connection::Sender *snd, kym::connection::Receiver *rcv, int count, int size);

kym::StatusOr<uint64_t> test_bw_send(kym::connection::Sender *snd, int count, int size, int unack);
kym::StatusOr<uint64_t> test_bw_batch_send(kym::connection::BatchSender *snd, int count, int size, int batch, int unack);
kym::StatusOr<uint64_t> test_bw_recv(kym::connection::Receiver *rcv, int count, int size);


#endif // KYM_BENCH_BENCH_HPP
