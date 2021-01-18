set print $median
msgs = "16 32 64 128 256 512 1024 2048 4096"
do for [i in msgs] {
  lat = sprintf("data/write-atomic-lat/write-atomic-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}
set print $mediandm
do for [i in msgs] {
  lat = sprintf("data/write-atomic-lat/write-atomic-lat-single-dm-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/write-atomic-lat-msgsize.png"

set xlabel "Msg size (bytes)" 
set ylabel "Latency (us)"
set yrange [5.5 : 8]
set ytics 0, 0.5, 20

set xtics 1, 2, 8192

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
plot $median  with points pt 5 ps 1.5 title "metadata in RAM", \
    $mediandm with points pt 7 ps 1.5 title "metadata in DM"

     
