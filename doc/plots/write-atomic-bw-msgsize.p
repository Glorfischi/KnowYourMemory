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

set terminal png small size 960,640 enhanced
set output "plots/write-atomic-bw-msgsize.png"

set title "Write Atomic Bandwidth" 
set xlabel "Message size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set xrange [16:20000]

set key left top

set logscale x 2
set xtics 1, 2, 16384
plot $datadm title "metadata on DM", \
     $data title "metadata on RAM", \
