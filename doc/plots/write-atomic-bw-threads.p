set print $data
conns = "1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16"
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--size-16-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $datadm
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--dm-size-16-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--size-512-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512dm
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--dm-size-512-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data8192
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--size-8192-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}
set print $data8192dm
do for [c in conns] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-conn-%s--dm-size-8192-unack-%s-server", c, c);
  stats bw using 4 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set terminal png small size 960,640  font "Computer Modern,16"
set output "plots/write-atomic-bw-threads.png"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set xlabel "Connections" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [0.5:15.5]
set xtics 0, 1, 16

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key right center

plot $data8192 title "8192 bytes" pt 9 ps 1.5, \
     $data8192dm title "8192 bytes \w DM" pt 4 ps 1.5, \
     $data512 title "512 bytes" pt 7 ps 1.5, \
     $data512dm title "512 bytes \w DM" pt 12 ps 1.5, \
     $data title "16 bytes" pt 5 ps 1.5, \
     $datadm title "16 bytes \w DM" pt 10 ps 1.5
