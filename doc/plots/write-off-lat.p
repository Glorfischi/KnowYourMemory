write_ping16_read=system("ls data/write-write-read-magicbuf-pingpong-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeOff_ping16_read=system("ls data/write-writeOff-read-magicbuf-pingpong-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeOff_client16_read=system("ls data/write-writeOff-read-magicbuf-lat-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeOff_server16_read=system("ls data/write-writeOff-read-magicbuf-lat-size16-server-ault02-2020-09-30T13:30:04Z.data")
writeImm_ping16_read=system("ls data/write-writeImm-read-magicbuf-pingpong-size16-client-ault02-2020-10-06T08:54:25Z.data")
send_ping16=system("ls data/send-recv--pingpong-size16-batch20-client-ault03-2020-10-01T09:58:44Z.data")


set terminal png small size 960,640 enhanced
set output "plots/write-off-lat.png"

set xlabel "Latency (us)" enhanced

stats write_ping16_read using 0 nooutput
set yrange [*:*]
set ytics 0,.2,1.3
set format y "%10.1f"


set xrange [0:15]
set title "Write Latency" noenhanced 

plot write_ping16_read using ($1):(1./STATS_max) smooth cumul title "PingPong WriteCRC", \
     send_ping16 using ($1):(1./STATS_max) smooth cumul title "PingPong Send", \
     writeOff_ping16_read using ($1):(1./STATS_max) smooth cumul title "PingPong WriteOff", \
     writeOff_client16_read using ($1):(1./STATS_max) smooth cumul title "client WriteOff", \
     writeOff_server16_read using ($1):(1./STATS_max) smooth cumul title "server WriteOff"


