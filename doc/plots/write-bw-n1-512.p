set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"
set print $writeRev
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeRev-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
set print $writeImm
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeImm-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
set print $writeOff
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeOff-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $atomic
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--size-512-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $atomicdm
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--dm-size-512-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}



set terminal png small size 960,640 font "Computer Modern,24"
set output "plots/write-bw-n1-512.png"

set xlabel "Senders" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [0.5:15.5]
set xtics 1, 2, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top
plot $writeRev title "BW-Rev" pt 5 ps 2, \
     $writeImm title "BW-Imm" pt 7 ps 2, \
     $writeOff title "BW-Off" pt 9 ps 2, \
     $atomicdm title "SW-DM" pt 10 ps 2, \
     $atomic title "SW" pt 5 ps 2, \
