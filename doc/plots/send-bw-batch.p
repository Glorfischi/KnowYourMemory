set print $data256
bat = "1 2 4 8 16 32"
do for [b in bat] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-256-batch-%s-server", b)
  stats bw using 5 noout
  print sprintf("%s %f\n", b, STATS_median*8/1024/1024/1024)
}
set print $data128
do for [b in bat] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-128-batch-%s-server", b)
  stats bw using 5 noout
  print sprintf("%s %f\n", b, STATS_median*8/1024/1024/1024)
}
set print $data64
do for [b in bat] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-64-batch-%s-server", b)
  stats bw using 5 noout
  print sprintf("%s %f\n", b, STATS_median*8/1024/1024/1024)
}
set print $data32
do for [b in bat] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-32-batch-%s-server", b)
  stats bw using 5 noout
  print sprintf("%s %f\n", b, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/send-bw-batch.png"

set title "Send Receive Bandwidth" 
set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced

set xtics (1,2,4,8,16,32)

plot $data256 title "256 unack" with linespoints ls 1,  \
     $data128 title "128 unack"with linespoints ls 2,  \
     $data64 title "64 unack"with linespoints ls 3,    \
     $data32 title "32 unack"with linespoints ls 4
