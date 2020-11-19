bw="data/write-bw/write-bw-writeRev-read-server.data"


set terminal png large size 1920,640 enhanced
set output "plots/write-bw-msg-size.png"

set multiplot layout 1,3 

set xlabel "Message Size (Bytes)" enhanced
set ylabel "Bandwith (Gbit/s)" enhanced

set key left top
set logscale x 2

set title "WriteRev Bandwith" noenhanced 

x0 = y0 = NaN
plot bw using ($3==128 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 128", \
     bw using ($3==64 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 64", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 1"


set title "WriteImm Bandwith" noenhanced 

bw="data/write-bw/write-bw-writeImm-read-server.data"

x0 = y0 = NaN
plot bw using ($3==128 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 128", \
     bw using ($3==64 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 64", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 1"

set title "WriteOff Bandwith" noenhanced 

bw="data/write-bw/write-bw-writeOff-read-server.data"

x0 = y0 = NaN
plot bw using ($3==128 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 128", \
     bw using ($3==64 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 64", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 1"




