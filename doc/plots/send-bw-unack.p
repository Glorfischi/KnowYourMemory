set print $unack
unack = "1 4 8 16 32 64 128"
do for [u in unack] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-16-unack-%s-batch-1-server", u)
  stats bw using 5 noout
  print sprintf("%s %f\n", u, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/send-bw-unack.png"

set title "Send Receive Bandwidth" 
set xlabel "Unacknowledged messages" 
set ylabel "Bandwith (Gbit/s)" enhanced

set xtics (1,2,4,8,16,32,64,128)

plot $unack  notitle
