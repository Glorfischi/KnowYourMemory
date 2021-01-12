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

set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/send-bw-batch.png"

set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set xtics (1,2,4,8,16,32)

set xrange [0.5:32.5]

set key top right

plot $data256 title "256 unack" pt 5 ps 1.5 ,  \
     $data128 title "128 unack" pt 7 ps 1.5,  \
     $data64 title "64 unack" pt 9 ps 1.5,    \
     $data32 title "32 unack" pt 13 ps 1.5
