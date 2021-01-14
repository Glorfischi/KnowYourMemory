set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"
set print $writeRev
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeRev-ack-read-unack-64-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
set print $writeImm
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeImm-ack-read-unack-64-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
set print $writeOff
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-512-writeOff-ack-read-unack-64-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}




set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/write-bw-threads-512.png"

set xlabel "Threads" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:120]
set ytics 0, 10, 100
set xrange [0.5:15.5]
set xtics 0, 1, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top
plot $writeRev title "WriteRev" pt 5 ps 1.5, \
     $writeImm title "writeImm" pt 7 ps 1.5, \
     $writeOff title "writeOff" pt 9 ps 1.5
