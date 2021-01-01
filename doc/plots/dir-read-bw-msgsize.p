
set print $data
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/read-bw/read-bw-size-%s-unack-64-unack-rcv-32-server.data", s);
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}
set print $dfen
msgs = "16 32 64 128 256 512 1024 2048 4096 8192 16384"
do for [s in msgs] {
  bw = sprintf("data/read-bw/read-bw-fence-size-%s-unack-64-unack-rcv-32-server.data", s);
  stats bw using 5 noout
  print sprintf("%s %f\n", s, STATS_median*8/1024/1024/1024)
}




set terminal png small size 960,640 enhanced
set output "plots/dir-read-bw-msgsize.png"

set title "Direct Read Bandwidth" 
set xlabel "Message size" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 5, 100 
set xrange [16:20000]

set key left top

set logscale x 2
set xtics 1, 2, 16384
plot $data title "unsignaled write", \
     $dfen title "fenced write"
