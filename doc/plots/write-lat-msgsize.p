msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
set print $writeOff
do for [i in msgs] {
  lat = sprintf("data/write-lat/write-lat-single-size-%s-writeOff-ack-send-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}
set print $writeImm
do for [i in msgs] {
  lat = sprintf("data/write-lat/write-lat-single-size-%s-writeImm-ack-send-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $writeRev
do for [i in msgs] {
  lat = sprintf("data/write-lat/write-lat-single-size-%s-writeRev-ack-send-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}
set terminal png small size 960,640  font "Computer Modern,16" 

set output "plots/write-lat-msgsize.png"

#set title "Buffered Write Latency" 
set xlabel "Msg size (bytes)" 
set ylabel "Latency (us)"
set yrange [1.5 : 7]
set ytics 0, 0.5, 6

set xtics 1, 2, 16384
set xrange [13 : 9500]


set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
f(x)=b*x + o
b=0.00023;o=2.2
fit f(x) $writeOff using 1:2 via b, o
plot $writeOff with points pt 5 ps 1.5 title "WriteOff", \
     $writeImm with points pt 7 ps 1.5  title "WriteImm", \
     $writeRev with points pt 9 ps 1.5  title "WriteRev"
