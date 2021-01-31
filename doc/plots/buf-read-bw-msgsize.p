set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/buf-read-bw/buf-read-bw-single-size-%s-server", s);
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set print $msgsize
do for [s in msgs] {
  bw = sprintf("data/buf-read-bw/buf-read-bw-single-size-%s-server", s);
  stats bw using 4 noout
  print sprintf("%s %f\n", s, STATS_median)
}


set terminal png small size 1120,640 font "Computer Modern,24" 
set output "plots/buf-read-bw-msgsize.png"

set xlabel "Message size (bytes)" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100 nomirror
set xrange [16:20000]


set y2label "Mean Transfer Size (KB)" enhanced

set y2tics 0, 20,  200
set y2range [0:200]

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
set xtics 1, 4, 16384
plot $data with points pt 5 ps 2 title "Bandwidth", \
     $msgsize u ($1):($2/1024) with points pt 6 ps 2 axes x1y2 title "Mean Transfer Size"
