set print $median
msgs = "16 32 64 128 256 512 1024 2048 4096 8192"
do for [i in msgs] {
  lat = sprintf("data/sendrcv-lat/sendRcv-lat-single-size-%s-client", i)
  stats lat using 1 noout
  print sprintf("%s %f\n", i, STATS_median/2)
}

set terminal pngcairo size 960,640 font "Computer Modern,16" 
set output "plots/send-lat-msgsize.png"

set xlabel "Message size (bytes)" 
set ylabel "Latency (μs)" enhanced
set yrange [1.5 : 5]
set ytics 0, 0.5, 6

set xtics 1, 2, 8192
set xrange [13 : 9500]


set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
f(x)=b*x + osnd + orcv + L + onsnd + onrcv


b=0.0002;
osnd=0.19;
orcv=0.007;
L=1.9;
onsnd=0.02;
onrcv=0.02;
g=6;

#fit f(x) $median using 1:2 via b
plot $median  with points  pt 5 ps 1.5 title "Data", \
  f(x) title "Model"
