#set terminal pngcairo transparent enhanced font "arial,10" fontscale 1.0 rounded size 900, 300
set terminal svg fname 'Helvetica' fsize 9 rounded size 900, 300
set output "reads.svg"
set datafile separator ","


set style line 80 lt 0
set style line 80 lt rgb "#000000"
set style line 81 lt 3
set style line 81 lt rgb "#606060" lw 0.5
set grid back linestyle 81
set border 3 back linestyle 80
set xtics nomirror
set ytics nomirror


set style line 1 lt rgb "#000000" lw 1 pt 7 pi -1 ps 0.5
set style line 5 lt rgb "#000000" lw 1 pt 2 ps 1

set style line 2 lt rgb "#00A000" lw 1 pt 9
set style line 3 lt rgb "#5060D0" lw 1 pt 5
set style line 4 lt rgb "#F25900" lw 1 pt 13

set multiplot layout 1, 2 title "Read Performance (1M file, different chunks)"
set bmargin 3

unset bars	
set style fill transparent solid 0.6 noborder
set ylabel "Time (ms)"
set yrange [0:*]
set xtics ('32' 0, '40' 1, '64' 2, '128' 3)
set grid

bs = 0.3 # width of a box

plot 'reads.stats.1' u ($0-bs):2:(bs) w boxes lc rgb"green" title "LFS", \
     'reads.stats.1' u ($0-bs):2:4 w yerrorbars notitle ls 1, \
     'reads.stats.1' u ($0-bs):3 w point notitle ls 5, \
     'reads.stats.1' u ($0):5:(bs) w boxes lc rgb"red" title "ext4", \
     'reads.stats.1' u ($0):5:7 w yerrorbars notitle ls 1, \
     'reads.stats.1' u ($0):6 w point notitle ls 5, \
     'reads.stats.1' u ($0+bs):8:(bs) w boxes lc rgb"blue" title "LFS (kcache)", \
     'reads.stats.1' u ($0+bs):8:10 w yerrorbars notitle ls 1, \
     'reads.stats.1' u ($0+bs):9 w point notitle ls 5

set xtics ('256' 0, '512' 1, '1K' 2, '2K' 3, '3K' 4, '4K' 5, '8K' 6, '16K' 7, '32K' 8)

set noylabel
set notitle 
set nokey

set label "Chunk Size (bytes)" at screen 0.5,0.05 center front

plot 'reads.stats.2' u ($0-bs):2:(bs) w boxes lc rgb"green" title "LFS", \
     'reads.stats.2' u ($0-bs):2:4 w yerrorbars notitle ls 1, \
     'reads.stats.2' u ($0-bs):3 w point notitle ls 5, \
     'reads.stats.2' u ($0):5:(bs) w boxes lc rgb"red" title "ext4", \
     'reads.stats.2' u ($0):5:7 w yerrorbars notitle ls 1, \
     'reads.stats.2' u ($0):6 w point notitle ls 5, \
     'reads.stats.2' u ($0+bs):8:(bs) w boxes lc rgb"blue" title "LFS (kcache)", \
     'reads.stats.2' u ($0+bs):8:10 w yerrorbars notitle ls 1, \
     'reads.stats.2' u ($0+bs):9 w point notitle ls 5

unset multiplot
