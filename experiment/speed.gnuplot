set terminal pngcairo transparent enhanced font "arial,10" fontscale 1.0 rounded size 1000, 500

set title "Transfer Speeds (src and dst)"
set output plotsdir . "/speed.png"

set ylabel "Speed (MiB/s)"
set xlabel "Time in experiment (s)"

set datafile separator " "
set timefmt "%s"
set format x "%s"
set key right top
set grid

plot logdir . "/src/speed.parsed" using 2:($4/1024/1024) with lines lw 2 lt 1 axes x1y1 title 'upload speed (src)', \
	 logdir . "/dst/speed.parsed" using 2:($21/1024/1024) with lines lw 2 lt 2 axes x1y1 title 'dwload speed (dst)'

reset

set terminal pngcairo transparent enhanced font "arial,10" fontscale 1.0 rounded size 1000, 300

set title "Download Progress (dst)"
set output plotsdir . "/progress.png"

set ylabel "Progress (%)"
set xlabel "Time in experiment (s)"

set datafile separator " "
set timefmt "%s"
set format x "%s"
set nokey
set grid

set yrange [0.0001:100]
set logscale y
set format y "%g %%"

plot logdir . "/dst/speed.parsed" using 2:22 with lines lw 2 lt 3 axes x1y1