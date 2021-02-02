msgs = "16 32 64 128 256 512 1024 2048 4080 8160 16120"
set print $writeRev
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeRev-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
set print $writeImm
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeImm-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}
set print $writeOff
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeOff-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}


set terminal png small size 960,640 font "Computer Modern,24"
set output "plots/write-bw-msgsize.png"

set xlabel "Message Size (bytes)" 
set ylabel "Bandwidth (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]
set ytics 0, 10, 100

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
set xtics 1, 4, 16384
plot $writeRev title "BW-Rev" pt 5 ps 2, \
     $writeImm title "BW-Imm" pt 7 ps 2, \
     $writeOff title "BW-Off" pt 9 ps 2
