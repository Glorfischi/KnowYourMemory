set terminal png small size 640,480
set output "plot/latency.png"
set multiplot layout 3,2 rowsfirst
filelist=system("ls data/sr_test_lat_send*.csv")
do for [filename in filelist] {
  plot sprintf("< sort -n %s",filename) title filename noenhanced
}

