set print $median
msgs = "16 32 64 128 256 512 1024 2048 4096 8192"
do for [i in msgs] {
  lat = sprintf("data/buf-read-lat/buf-read-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set terminal png small size 960,640 enhanced
set output "plots/buf-read-lat-msgsize.png"

set title "Buffered Read Latency" 
set xlabel "Msg size (bytes)" 
set ylabel "Latency (us)"
set yrange [0 : 15]
set ytics 0, 0.5, 20

set xtics 1, 2, 8192

set logscale x 2
plot $median  with points pt 4 notitle 

     
