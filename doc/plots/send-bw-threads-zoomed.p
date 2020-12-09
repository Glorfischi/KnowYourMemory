set print $data
conns = "1 2 3 4 5"
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $datasrq
conns = "1 2 3 4 5"
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-srq-batch-size-16-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-512-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set print $data512srq
do for [c in conns] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-srq-batch-size-512-unack-64-batch-1-conn-%s-server", c )
  stats bw using 5 noout
  print sprintf("%s %f\n", c, STATS_sum*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/send-bw-threads-zoomed.png"

set title "Send Receive Bandwidth" 
set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:20]
set ytics 0, .5, 100
set xrange [0.5:5.5]

set key left center

plot $data512 title "512 bytes", \
     $data512srq title "512 bytes \w SRQ", \
     $data title "16 bytes", \
     $datasrq title "16 bytes \w SRQ"
