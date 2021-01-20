set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"
do for [c in conns] {
  bw = sprintf("data/read-bw/read-bw-single-receive-conn-%s-size-16-unack-64-unack-rcv-32-server.data", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512
do for [c in conns] {
  bw = sprintf("data/read-bw/read-bw-single-receive-conn-%s-size-512-unack-64-unack-rcv-32-server.data", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data8192
do for [c in conns] {
  bw = sprintf("data/read-bw/read-bw-single-receive-conn-%s-size-8192-unack-64-unack-rcv-32-server.data", c);
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}


set terminal png small size 960,640 enhanced
set output "plots/dir-read-bw-n1.png"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set title "Direct Read Bandwidth (N:1)" 
set xlabel "Connections" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [0.5:15.5]
set xtics 0, 1, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"
set key right center


plot $data8192 pt 5 ps 1.5 title "8192 bytes", \
     $data512 pt 7 ps 1.5 title "512 bytes", \
     $data pt 9 ps 1.5 title "16 bytes"


