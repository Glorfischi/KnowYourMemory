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

set terminal png small size 960,640  font "Computer Modern,16" 
set output "plots/dir-read-lat-msgsize.png"
set xlabel "Message Size" 
set ylabel "Latency (us)"
set yrange [4 : 12]
set ytics 0, 0.5, 12
set xrange [15 : 9000]

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
set xtics 1, 2, 16384


plot $fence with points pt 6 ps 1.5 title "w/  fence", \
     $median with points pt 5 ps 1.5 title "w/o fence"
