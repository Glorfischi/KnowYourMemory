bw32=system("ls data/sr-bw-aggr/send-recv--bw-size32.data")
bw64=system("ls data/sr-bw-aggr/send-recv--bw-size64.data")
bw1024=system("ls data/sr-bw-aggr/send-recv--bw-size1024.data")
bw4096=system("ls data/sr-bw-aggr/send-recv--bw-size4096.data")
bw8192=system("ls data/sr-bw-aggr/send-recv--bw-size8192.data")

srqbw32=system("ls data/sr-bw-aggr/send-recv-srq-bw-size32.data")
srqbw64=system("ls data/sr-bw-aggr/send-recv-srq-bw-size64.data")
srqbw1024=system("ls data/sr-bw-aggr/send-recv-srq-bw-size1024.data")
srqbw4096=system("ls data/sr-bw-aggr/send-recv-srq-bw-size4096.data")
srqbw8192=system("ls data/sr-bw-aggr/send-recv-srq-bw-size8192.data")



set terminal png small size 960,640 enhanced
set output "plots/send-bw.png"

set xlabel "Batch Size" enhanced
set ylabel "Bandwith (Gbit/s)" enhanced


set title "Send Receive Bandwith" noenhanced 

plot bw64 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 64", \
     srqbw64 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 64 with SRQ", \
     bw4096 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 4096", \
     srqbw4096 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 4096 with SRQ", \
     bw8192 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 8192", \
     srqbw8192 using ($1):(8*$2/1024/1024/1024) smooth unique with linespoints title "Message Size 8192 with SRQ"



