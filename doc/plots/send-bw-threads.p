set print $data
conns = "1 2 4 8 16 32"
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-256-batch-8-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-512-unack-256-batch-8-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data8192
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-8192-unack-256-batch-8-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/send-bw-threads.png"

set title "Send Receive Bandwidth" 
set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [1:35]

set key left top

plot $data8192 title "8192 bytes", \
     $data512 title "512 bytes", \
     $data title "16 bytes"
