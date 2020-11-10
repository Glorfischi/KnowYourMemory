bw="data/sendRcv-bw/sendRcv-bw-server.data"
bw_srq="data/sendRcv-bw/sendRcv-bw-srq-sever.data"



set terminal png small size 960,640 enhanced
set output "plots/send-bw.png"

set xlabel "Message Size (Bytes)" enhanced
set ylabel "Bandwith (Gbit/s)" enhanced

set logscale x 2

set title "Send Receive Bandwith" noenhanced 

x0 = y0 = NaN
plot bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/o SRQ", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/ SRQ, unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/ SRQ, unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/ SRQ, unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/ SRQ, unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "w/ SRQ, unack 1"



