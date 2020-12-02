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

set terminal png small size 960,640 enhanced
set output "plots/send-bw-msgsize.png"

set title "Send Receive Bandwidth" 
set xlabel "Batch size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]

set key left top

min(a,c) = (a < c) ? a : c
set logscale x 2
f(x)=x/(o + (x-1)*b) # device overhead
b=0.00023;o=2.2
g(x)=x/z # o_{snd}+g_{snd} when piplined
z=1
h(x)=x/(z+p+(x-1)*b) # I realized that the g_{snd} abstraction is not quite correct for this 
p=1
fit f(x) $databatch using 1:2 via b, o
fit[:3000] g(x) $data using 1:2 via z
fit h(x) $dataseq using 1:2 via p
set xtics 1, 2, 16384
plot $databatch title "batched", \
     $data title "unbatched", \
     $dataseq title "sequential", \
     f(x) title "Model device bottleneck", g(x) title "Model unbatched bottleneck", h(x) title "Model sequential bottleneck"
