set print $median
msgs = "16 32 64 128 256 512 1024 2048 4096 8192"
do for [i in msgs] {
  lat = sprintf("data/sendrcv-lat/sendRcv-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set terminal png small size 960,640 font "Computer Modern,16" 
set output "plots/send-lat-msgsize.png"

set title "Send Receive Latency" 
set xlabel "Msg size (bytes)" 
set ylabel "Latency (us)"
set yrange [1.5 : 5]
set ytics 0, 0.5, 6

set xtics 1, 2, 8192
set xrange [13 : 9500]

set logscale x 2
f(x)=b*x + o
b=0.00023;o=2.2
fit f(x) $median using 1:2 via b, o
plot $median  with points  pt 5 ps 1.5 notitle

#f(x) title "SendRcv model"
