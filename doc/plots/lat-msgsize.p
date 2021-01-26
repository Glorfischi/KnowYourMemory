set print $median
msgs = "16 32 64 128 256 512 1024 2048 4096  8192"
do for [i in msgs] {
  lat = sprintf("data/write-direct-lat/write-direct-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $send
do for [i in msgs] {
  lat = sprintf("data/sendrcv-lat/sendRcv-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $writeOff
do for [i in msgs] {
  lat = sprintf("data/write-lat/write-lat-single-size-%s-writeOff-ack-send-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $writeRev
do for [i in msgs] {
  lat = sprintf("data/write-lat/write-lat-single-size-%s-writeRev-ack-send-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}
set print $dirread
do for [i in msgs] {
  lat = sprintf("data/read-lat/read-lat-single-size-%s-client.data", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $bufread
do for [i in msgs] {
  lat = sprintf("data/buf-read-lat/buf-read-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set encoding utf8
set terminal pngcairo size 960,640 font "Computer Modern,16" 
set output "plots/lat-msgsize.png"
set xlabel "Message size (bytes)" 
set ylabel "Latency (μs)" enhanced
set yrange [2 : 12]
set ytics 0, 1, 20

set xtics 1, 2, 8192

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top
set logscale x 2
plot $bufread with linespoints pt 11 ps 1.5 title "BR", \
     $dirread with linespoints pt 9 ps 1.5 title "DR", \
     $writeOff with linespoints pt 5 ps 1.5 title "BR-Off", \
     $writeRev with linespoints pt 4 ps 1.5  title "BR-Rev", \
     $median with linespoints pt 13 ps 1.5 title "DW", \
     $send with linespoints pt 7 ps 1.5 title "SR", \


     
