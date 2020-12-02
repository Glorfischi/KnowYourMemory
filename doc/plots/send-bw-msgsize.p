set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/sendrcv-bw-param/sendRcv-bw-batch-size-%s-unack-64-batch-1-server", s)
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}

set terminal png small size 960,640 enhanced
set output "plots/send-bw-msgsize.png"

set title "Send Receive Bandwidth" 
set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]


min(a,c) = (a < c) ? a : c
set logscale x 2
f(x)=x/(o + x*b)
b=0.00023;o=2.2
g(x)=x/z
z=1
fit[4000:] f(x) $data using 1:2 via b, o
fit[:3000] g(x) $data using 1:2 via z

set xtics 1, 2, 16384
plot $data notitle, \
  f(x), g(x)
