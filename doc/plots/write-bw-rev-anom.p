msgs = "4080 4090 4095 4100 4110 4120 4130 4140 4150 4160 4170"
set print $writeRev
do for [s in msgs] {
  bw = sprintf("data/write-bw/write-bw-single-size-%s-writeRev-ack-read-unack-64-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}


set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/write-bw-rev-anom.png"

#set title "Write Bandwidth" 
set xlabel "Message Size (bytes)" 
set ylabel "Bandwidth (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [4075:4175]
set xtics 0, 10, 4180


set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top
plot $writeRev  pt 5 ps 1.5 notitle 
