bw_read="data/write-bw/write-bw-writeRev-read-server.data"
bw_send="data/write-bw/write-bw-writeRev-send-server.data"


set terminal png large size 1920,640 enhanced
set output "plots/write-bw-ack.png"

set multiplot layout 1,3 

set xlabel "Message Size (Bytes)" enhanced
set ylabel "Bandwith (Gbit/s)" enhanced

set key left top
set logscale x 2

set title "WriteRev Bandwith (unack 64)" noenhanced 

x0 = y0 = NaN
plot bw_read using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "read ack", \
     bw_send using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "send ack"



set title "WriteImm Bandwith" noenhanced 

bw_read="data/write-bw/write-bw-writeImm-read-server.data"
bw_send="data/write-bw/write-bw-writeImm-send-server.data"


x0 = y0 = NaN
plot bw_read using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "read ack", \
     bw_send using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "send ack"

set title "WriteOff Bandwith" noenhanced 

bw_read="data/write-bw/write-bw-writeOff-read-server.data"
bw_send="data/write-bw/write-bw-writeOff-send-server.data"

x0 = y0 = NaN
plot bw_read using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "read ack", \
     bw_send using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "send ack"

