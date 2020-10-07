writeImm_read_client=system("ls data/write-writeImm-read-magicbuf-lat-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeImm_read_server=system("ls data/write-writeImm-read-magicbuf-lat-size16-server-ault02-2020-09-30T13:30:04Z.data")
writeImm_send_client=system("ls data/write-writeImm-send-magicbuf-lat-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeImm_send_server=system("ls data/write-writeImm-send-magicbuf-lat-size16-server-ault02-2020-09-30T13:30:04Z.data")

set terminal png small size 960,640 enhanced
set output "plots/write-ack-lat.png"

set xlabel "Latency (us)" enhanced

stats writeImm_read_client using 0 nooutput
# set ytics 0,.00001,1.0
set format y "%10.5f"
set yrange [0.99:1.0]


set xrange [0:15]
set title "Acknowledgement Latency" noenhanced 

plot writeImm_read_client using ($1):(1./STATS_max) smooth cumul title "Read Client", \
     writeImm_read_server using ($1):(1./STATS_max) smooth cumul title "Read Server", \
     writeImm_send_client using ($1):(1./STATS_max) smooth cumul title "Send Client", \
     writeImm_send_server using ($1):(1./STATS_max) smooth cumul title "Send Server"


