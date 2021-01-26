set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15"
set print $writeImm
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-16-writeImm-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
set print $writeOff
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-16-writeOff-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
set print $writeRev
do for [c in conns] {
  bw = sprintf("data/write-bw/write-bw-conn-%s-size-16-writeRev-ack-send-unack-64-single-receive-server", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $atomic
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--size-16-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $atomicdm
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--dm-size-16-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}




set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/write-bw-n1-16.png"

set xlabel "Senders" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:5]
set ytics 0, 0.5, 10
set xrange [0.5:15.5]
set xtics 0, 1, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top
plot $writeRev title "BW-Rev" pt 5 ps 1.5, \
     $writeImm title "BW-Imm" pt 7 ps 1.5, \
     $writeOff title "BW-Off" pt 9 ps 1.5, \
     $atomicdm title "SW-DM" pt 10 ps 1.5, \
     $atomic title "SW" pt 5 ps 1.5, \
