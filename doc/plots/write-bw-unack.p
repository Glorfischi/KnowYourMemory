unack = "1 4 8 16 32 64 128"
set print $writeOff
do for [u in unack] {
  bw = sprintf("data/write-bw/write-bw-single-size-16-writeOff-ack-send-unack-%s-client", u)
  stats bw using 5 noout
  print sprintf("%s %f\n", u, STATS_median*8/1024/1024/1024)
}
set print $writeImm
do for [u in unack] {
  bw = sprintf("data/write-bw/write-bw-single-size-16-writeImm-ack-send-unack-%s-client", u)
  stats bw using 5 noout
  print sprintf("%s %f\n", u, STATS_median*8/1024/1024/1024)
}
set print $writeRev
do for [u in unack] {
  bw = sprintf("data/write-bw/write-bw-single-size-16-writeRev-ack-send-unack-%s-client", u)
  stats bw using 5 noout
  print sprintf("%s %f\n", u, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/write-bw-unack.png"

set title "Write Receive Bandwidth" 
set xlabel "Unacknowledged messages" 
set ylabel "Bandwith (Gbit/s)" enhanced

set xrange [0:130]
set xtics (1,2,4,8,16,32,64,128)

plot $writeOff  title "WriteOff", \
     $writeImm  title "WriteImm", \
     $writeRev  pt 4 title "WriteRev"
