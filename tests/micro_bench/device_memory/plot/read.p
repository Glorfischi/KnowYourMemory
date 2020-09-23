ramdata="data/read-ram.data"
dmdata="data/read-dm.data"

set terminal png small size 960,640 enhanced
set output "plot/read.png"

set xlabel "Latency (us)" enhanced

stats ramdata using 0 nooutput
set yrange [*:*]
set ytics 0,.2,1.3
set format y "%10.1f"

set title "Read Latency" noenhanced 

plot ramdata using ($1):(1./STATS_max) smooth cumul title "from RAM", \
     dmdata using ($1):(1./STATS_max) smooth cumul title "from DM"

