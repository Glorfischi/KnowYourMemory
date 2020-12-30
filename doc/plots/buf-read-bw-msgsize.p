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


set terminal png small size 960,640 enhanced
set output "plots/buf-read-bw-msgsize.png"

set title "Buffered Read Bandwidth" 
set xlabel "Message size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 5, 100 nomirror
set xrange [16:20000]


set y2label "Mean Transfer Size (byte)" enhanced

set y2tics 0, 8196,  191575

set key left top

set logscale x 2
set xtics 1, 2, 16384
plot $data title "Bandwidth", $msgsize axes x1y2 title "Mean Transfer Size"
