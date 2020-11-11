
set terminal png large size 1280,640 enhanced
set output "plots/read-bw.png"

set multiplot layout 1,2 

set xlabel "Message Size (Bytes)" enhanced
set ylabel "Bandwith (Gbit/s)" enhanced

set key left top
set logscale x 2

set title "Read Bandwith" noenhanced 
bw="data/read-dev/read-bw-server.data"

x0 = y0 = NaN
plot bw using ($3==128 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 128", \
     bw using ($3==64 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 64", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 1"


set title "Read Bandwith w/ fence" noenhanced 
bw="data/read-dev/read-bw-fence-server.data"


x0 = y0 = NaN
plot bw using ($3==128 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 128", \
     bw using ($3==64 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 64", \
     bw using ($3==32 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 32", \
     bw using ($3==16 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 16", \
     bw using ($3==8 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 8", \
     bw using ($3==4 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 4", \
     bw using ($3==1 ? (y0=8*$5/1024/1024/1024,x0=$2) : x0):(y0) smooth unique with linespoints title "unack 1"

