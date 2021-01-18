set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-single-size-%s-unack-2-client", s);
  stats bw using 4 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}


set print $datadm
do for [s in msgs] {
  bw = sprintf("data/write-atomic-bw/write-atomic-lat-single-dm-size-%s-unack-2-client", s);
  stats bw using 4 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 font "Computer Modern,16"
set output "plots/write-atomic-bw-msgsize.png"

set xlabel "Message size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"



set logscale x 2
set xtics 1, 2, 16384
plot $data pt 5 ps 1.5 title "metadata on RAM", \
     $datadm pt 7 ps 1.5 title "metadata on DM", \
