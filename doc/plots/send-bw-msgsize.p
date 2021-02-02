set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-%s-unack-64-batch-1-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set print $databatch
do for [s in msgs] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-%s-unack-256-batch-8-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}


set print $dataseq
do for [s in msgs] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-%s-unack-1-batch-1-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set terminal pngcairo size 960,640 font "Computer Modern,16" 
set output "plots/send-bw-msgsize.png"

set xlabel "Message size (bytes)" 
set ylabel "Bandwidth (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 5, 100
set xrange [16:20000]


set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set key left top

min(a,c) = (a < c) ? a : c
set logscale x 2
b=0.00023;o=2.2

z=1


#piplined
g(x)=x/osnd*1000*1000*8/1024/1024/1024
# Sequential
h(x)=(x/(osnd+g+x*gl))*1000*1000*8/1024/1024/1024
# Batched
f(x)=x/(onsnd + (x)*b)*1000*1000*8/1024/1024/1024


b=0.000082;
osnd=0.19;
orcv=0.17;
L=1.9;
onsnd=0.02;
onrcv=0.02;
g=5;
gl=0.02

#fit[2000:] f(x) $databatch using 1:2 via onsnd
#fit[:3000] g(x) $data using 1:2 via osnd
fit h(x) $dataseq using 1:2 via g, gl
set xtics 1, 2, 16384
plot $databatch title "SR-Bat" pt 5 ps 1.5 , \
     $data title "SR" pt 7 ps 1.5 , \
     $dataseq title "SR-Seq" pt 9 ps 1.5, \
     f(x) title "Device Bottleneck", \
     g(x) title "CPU Bottleneck", \
     h(x) title "Round-Trip Bottleneck"
