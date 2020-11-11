ping16="data/read-dev/read-pingpong-16-client.data"
ping16fence="data/read-dev/read-pingpong-fence-16-client.data"

ping64="data/read-dev/read-pingpong-64-client.data"
ping64fence="data/read-dev/read-pingpong-fence-64-client.data"


set terminal png size 960,640 enhanced
set output "plots/read-pingpong.png"

set xlabel "Latency (us)" enhanced

stats ping16 using 0 nooutput
set yrange [*:*]
set ytics 0,.2,1.3
set format y "%10.1f"


set xrange [10:20]
set xtics 0,1,30
set title "Write PingPong Latency" noenhanced 

plot ping16 using ($1):(1./STATS_max) smooth cumul title " msg\_len 16 Bytes", \
     ping64 using ($1):(1./STATS_max) smooth cumul title "msg\_len 64 Bytes", \
     ping16fence using ($1):(1./STATS_max) smooth cumul title "Fence msg\_len 16 Bytes", \
     ping64fence using ($1):(1./STATS_max) smooth cumul title "Fence msg\_len 64 Bytes"



