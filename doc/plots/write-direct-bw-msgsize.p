set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/write-direct-bw/write-direct-bw-single-size-%s-unack-64-server", s);
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set print $send
do for [s in msgs] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-%s-unack-64-batch-1-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 font "Computer Modern,24" 
set output "plots/write-direct-bw-msgsize.png"

set xlabel "Message Size (bytes)" 
set ylabel "Bandwidth (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [14:20000]

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
set xtics 1, 4, 16384
plot $data with points pt 5 ps 2 title "Direct Write (DW)", \
     $send with points pt 6 ps 2 title "Send-Receive (SR)" 
