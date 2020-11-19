ping_srq_1="data/send-multithread/sendRcv-lat-conn-1-srq-client"
#stats ping_srq_1 using 0 prefix "ping_srq_1"
ping_srq_2="data/send-multithread/sendRcv-lat-conn-2-srq-client"
#stats ping_srq_2 using 0 prefix "ping_srq_2"
ping_srq_4="data/send-multithread/sendRcv-lat-conn-4-srq-client"
#stats ping_srq_4 using 0 prefix "ping_srq_4"
ping_srq_8="data/send-multithread/sendRcv-lat-conn-8-srq-client"
#stats ping_srq_8 using 0 prefix "ping_srq_8"
ping_srq_16="data/send-multithread/sendRcv-lat-conn-16-srq-client"
#stats ping_srq_16 using 0 prefix "ping_srq_16"
ping_srq_32="data/send-multithread/sendRcv-lat-conn-32-srq-client"
#stats ping_srq_32 using 0 prefix "ping_srq_32"
ping_srq_64="data/send-multithread/sendRcv-lat-conn-64-srq-client"
#stats ping_srq_64 using 0 prefix "ping_srq_64"
ping_srq_128="data/send-multithread/sendRcv-lat-conn-128-srq-client"
#stats ping_srq_128 using 0 prefix "ping_srq_128"

ping_1="data/send-multithread/sendRcv-lat-conn-1-client"
#stats ping_1 using 0 prefix "ping_1"
ping_2="data/send-multithread/sendRcv-lat-conn-2-client"
#stats ping_2 using 0 prefix "ping_2"
ping_4="data/send-multithread/sendRcv-lat-conn-4-client"
#stats ping_4 using 0 prefix "ping_4"
ping_8="data/send-multithread/sendRcv-lat-conn-8-client"
#stats ping_8 using 0 prefix "ping_8"
ping_16="data/send-multithread/sendRcv-lat-conn-16-client"
#stats ping_16 using 0 prefix "ping_16"
ping_32="data/send-multithread/sendRcv-lat-conn-32-client"
#stats ping_32 using 0 prefix "ping_32"
ping_64="data/send-multithread/sendRcv-lat-conn-64-client"
#stats ping_64 using 0 prefix "ping_64"
ping_128="data/send-multithread/sendRcv-lat-conn-128-client"
#stats ping_128 using 0 prefix "ping_128"

ping=system("ls -- data/send-multithread/*[0-9]-client")
ping_srq=system("ls -- data/send-multithread/*[0-9]-srq-client")


set terminal png small size 1280,640 enhanced
set output "plots/send-pingpong-multi.png"
set multiplot layout 1, 2

set title "Send Receive Latency" 
set xlabel "# Connections" 
set ylabel "Latency (us)"

set xrange [0.5:256]
set xtics 1, 2, 128

set yrange [0.:180]


set logscale x 2
set style data boxplot
set style boxplot nooutliers
plot ping_1 using (1.0):1 notitle, \
     ping_2 using (2.0):1 notitle, \
     ping_4 using (4.0):1 notitle, \
     ping_8 using (8.0):1 notitle, \
     ping_16 using (16.0):1 notitle, \
     ping_32 using (32.0):1 notitle, \
     ping_64 using (64.0):1 notitle, \
     ping_128 using (128.0):1 notitle, \


set title "Send Receive Latency (SRQ)" 
plot ping_srq_1 using (1.0):1 notitle, \
     ping_srq_2 using (2.0):1 notitle, \
     ping_srq_4 using (4.0):1 notitle, \
     ping_srq_8 using (8.0):1 notitle, \
     ping_srq_16 using (16.0):1 notitle, \
     ping_srq_32 using (32.0):1 notitle, \
     ping_srq_64 using (64.0):1 notitle, \
     ping_srq_128 using (128.0):1 notitle, \



