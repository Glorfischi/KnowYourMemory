writeImm_read=system("ls data/write-writeImm-read-magicbuf-pingpong-size16-client-ault02-2020-10-06T08:54:25Z.data")
writeImm_send=system("ls data/write-writeImm-send-magicbuf-pingpong-size16-client-ault02-2020-10-06T08:54:25Z.data")

set terminal png small size 960,640 enhanced
set output "plots/write-ack-ping.png"

set xlabel "Latency (us)" enhanced

stats writeImm_read using 0 nooutput
# set ytics 0,.00001,1.0
set format y "%10.5f"
set yrange [0.99:1.0]


set xrange [0:15]
set title "Acknowledgement Latency" noenhanced 

plot writeImm_read using ($1):(1./STATS_max) smooth cumul title "Read", \
     writeImm_send using ($1):(1./STATS_max) smooth cumul title "Send"


