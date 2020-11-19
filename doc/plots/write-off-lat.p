write_ping16_read=system("ls data/write-write-read-magicbuf-pingpong-size16-client-ault03-2020-10-01T09:58:44Z.data")
writeOff_ping16_read=system("ls data/write-writeOff-read-magicbuf-pingpong-size16-client-ault02-2020-10-29T12:27:29Z.data")
writeImm_ping16_read=system("ls data/write-writeImm-read-magicbuf-pingpong-size16-client-ault02-2020-10-29T12:27:29Z.data")
writeRev_ping16_read=system("ls data/write-writeRev-read-magicbuf-pingpong-size16-client-ault02-2020-10-29T12:27:29Z.data")
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

plot writeRev_ping16_read using ($1):(1./STATS_max) smooth cumul title "PingPong WriteRev", \
     send_ping16 using ($1):(1./STATS_max) smooth cumul title "PingPong Send", \
     writeImm_ping16_read using ($1):(1./STATS_max) smooth cumul title "PingPong WriteImm", \
     writeOff_ping16_read using ($1):(1./STATS_max) smooth cumul title "PingPong WriteOff"


