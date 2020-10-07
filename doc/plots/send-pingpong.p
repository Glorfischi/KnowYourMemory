ping16=system("ls data/send-recv--pingpong-size16-batch20-client-ault03-2020-10-01T09:58:44Z.data")
ping16srq=system("ls data/send-recv-srq-pingpong-size16-batch20-client-ault03-2020-10-01T09:58:44Z.data")


sndLat16=system("ls data/send-recv--lat-size16-batch20-client-ault03-2020-10-01T09:58:44Z.data");
sndLat16srq=system("ls data/send-recv-srq-lat-size16-batch20-client-ault03-2020-10-01T09:58:44Z.data");

set terminal png small size 960,640 enhanced
set output "plots/send-pingpong.png"

set xlabel "Latency (us)" enhanced

stats ping16 using 0 nooutput
set yrange [*:*]
set ytics 0,.2,1.3
set format y "%10.1f"


set xrange [0:15]
set title "Send Receive Latency" noenhanced 

plot ping16 using ($1):(1./STATS_max) smooth cumul title "PingPong", \
     ping16srq using ($1):(1./STATS_max) smooth cumul title "PingPong SRQ", \
     sndLat16 using ($1):(1./STATS_max) smooth cumul title "Send Latency", \
     sndLat16srq using ($1):(1./STATS_max) smooth cumul title "Send Latency SRQ"

