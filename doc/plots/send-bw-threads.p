set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-size-16-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}


set print $data512
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-size-512-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}


set print $data8192
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-size-8192-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/send-bw-threads.png"

set xlabel "Connections" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 5, 100
set xrange [0.5:15.5]
set xtics 0, 1, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key right center

plot $data8192 title "8192 bytes" pt 5 ps 1.5, \
     $data512 title "512 bytes" pt 7 ps 1.5, \
     $data title "16 bytes" pt 9 ps 1.5
