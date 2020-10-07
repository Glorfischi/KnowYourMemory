ramdata="data/write-ram.data"
dmdata="data/write-dm.data"

set terminal png small size 960,640 enhanced
set output "plot/write.png"

set xlabel "Latency (us)" enhanced

stats ramdata using 0 nooutput
set yrange [*:*]
set ytics 0,.2,1.3
set format y "%10.1f"

set title "Write Latency" noenhanced 

plot ramdata using ($1):(1./STATS_max) smooth cumul title "from RAM", \
     dmdata using ($1):(1./STATS_max) smooth cumul title "from DM"

