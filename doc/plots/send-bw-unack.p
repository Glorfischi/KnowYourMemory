set print $unack
unack = "1 4 8 16 32 64"
do for [u in unack] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-%s-batch-1-server", u)
  stats bw using 5 noout
  print sprintf("%s %f\n", u, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640  font "Computer Modern,16"
set output "plots/send-bw-unack.png"

set xlabel "Unacknowledged messages" 
set ylabel "Bandwith (Gbit/s)" enhanced

set xtics (1,2,4,8,16,32,64,128)
set xrange [0.5:64.5]

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"

plot $unack  pt 5 ps 1.5 notitle
