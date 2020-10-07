ramdata="data/atomic-ram.data"
dmdata="data/atomic-dm.data"

set terminal png small size 960, 640 enhanced
set output "plot/atomic.png"

set ylabel "Throughput MOps/s" enhanced
set xlabel "# Clients" enhanced

set xrange [0:17]
set yrange [0:8]

set title "Fetch and Add Throughput" noenhanced 

plot ramdata with linespoints title "to RAM", dmdata with linespoints title "to DM"

