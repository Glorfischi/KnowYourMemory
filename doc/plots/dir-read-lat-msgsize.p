msgs = "16 32 64 128 256 512 1024 2048 4096 8192"
set print $median
do for [i in msgs] {
  lat = sprintf("data/read-lat/read-lat-single-size-%s-client.data", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set print $fence
do for [i in msgs] {
  lat = sprintf("data/read-lat/read-lat-single-fence-size-%s-client.data", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set terminal png small size 960,640 enhanced
set output "plots/dir-read-lat-msgsize.png"

set title "Direct Read Latency" 
set xlabel "Msg size (bytes)" 
set ylabel "Latency (us)"
set yrange [0 : 15]
set ytics 0, 0.5, 20

set xtics 1, 2, 8192

set logscale x 2
plot $median  with points pt 4 title "unsignaled write", \
     $fence  with points pt 2 title "fenced write"
     
