
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



set terminal png small size 960,640 font "Computer Modern,16" 
set output "plots/dir-read-bw-msgsize.png"

set xlabel "Message Size (bytes)" 
set ylabel "Bandwith (Gbit/s)" enhanced
set yrange [0:100]
set ytics 0, 10, 100
set xrange [16:20000]

set key left top

set grid ytics lt 0 lw 1 lc rgb "#bbbbbb"
set grid xtics lt 0 lw 1 lc rgb "#bbbbbb"

set logscale x 2
set xtics 1, 2, 16384


a=0.1;b=3; o=1
f(x)= a*x # posting overhead
g(x)=x/(o + (x-1)*b) # device overhead
fit [:2000]f(x) $data using 1:2 via a
fit [1000:]g(x) $data using 1:2 via o, b


plot $data with points pt 6 ps 1.5 title "DW", \
     $dfen with points pt 5 ps 1.5 title "DW-Fence"
