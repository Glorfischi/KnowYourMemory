msgs = "16 32 64 128 256 512 1024 2048 4080 8160 16120"
set print $writeRev
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeRev-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
set print $writeImm
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeImm-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}
set print $writeOff
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeOff-ack-send-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}


set terminal png small size 960,640 enhanced
set output "plots/write-bw-msgsize.png"

set title "Write Bandwidth" 
set xlabel "Message Size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]

set key left top

set logscale x 2
set xtics 1, 2, 16384
plot $writeRev title "WriteRev", \
     $writeImm title "writeImm", \
     $writeOff title "writeOff"
