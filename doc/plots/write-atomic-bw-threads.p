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

set terminal png small size 960,640 enhanced
set output "plots/write-atomic-bw-threads.png"

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set title "Write Atomic Bandwidth" 
set xlabel "Connections" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 5, 100
set xrange [0.5:16.5]

set key left center

plot $data8192 title "8192 bytes", \
     $data8192dm title "8192 bytes \w DM", \
     $data512 title "512 bytes", \
     $data512dm title "512 bytes \w DM", \
     $data title "16 bytes", \
     $datadm title "16 bytes \w DM"
