set terminal pngcairo transparent enhanced font "arial,10" fontscale 1.0 rounded size 1000, 500

set output plotsdir . "/speed.png"

set tmargin 0
set bmargin 0
set lmargin 10
set rmargin 15

set datafile separator " "

set palette maxcolors 6
set palette defined (\
    0 '#A8DA16', \
    10 '#AB1181', \
    20  '#E69B17', \
    30  '#1A5396', \
    40  '#D71900', \
    50  '#000000')
set cbtics ("keep alive" 0, "ping pong" 1, "slow start" 2, "aimd" 3, "ledbat" 4, "close" 5)
set cbtics rotate by 45
set colorbox user vertical origin 0.78,0.28 size 0.02,0.2
set cbrange [-0.5:5.5]
set zrange [0:5]
set boxwidth 1 relative
set style fill transparent solid 0.65

set multiplot title "Transfer Statistics (src and dst)"

set size 1,0.68
set origin 0.0,0.25

set ylabel "Speed (MiB/s)"
set y2label "Hints (B)"
set logscale y2
set datafile separator " "
set key left bottom
set grid
set ytics
set y2tics

unset xtics

stats logdir . "/src/speed.parsed" using 2
set xr [STATS_min - 10:STATS_max + 10]

plot logdir . "/src/speed.parsed" using 2:($4/1024/1024) with lines lw 2 lt 1 axes x1y1 title 'upload speed (src)', \
	 logdir . "/dst/speed.parsed" using 2:($21/1024/1024) with lines lw 2 lt 2 axes x1y1 title 'dwload speed (dst)', \
	 logdir . "/dst/speed.parsed" using 2:23 with lines lw 2 lt 3 axes x1y2 title 'hints out (dst)', \
	 logdir . "/src/speed.parsed" using 2:5 with lines lw 2 lt 4 axes x1y2 title 'hints in (src)'

unset title
unset ylabel
unset logscale
unset key
unset y2label
unset y2tics
unset ytics

set yrange [0:1]

set size 1,0.05
set origin 0.0,0.20

plot logdir . "/dst/speed.parsed" using 2:(0):24 with boxes lc palette title 'send control (dst)', \
	 logdir . "/src/speed.parsed" using 2:(1):6 with boxes lc palette title 'send control (src)'

set xlabel "Time in experiment (s)"

unset colorbox

set size 1,0.05
set origin 0.0,0.15

set timefmt "%d"
set format x "%s"
set xtics

plot logdir . "/dst/speed.parsed" using 2:(1):24 with boxes lc palette title 'send control (dst)', \
	 logdir . "/src/speed.parsed" using 2:(0):6 with boxes lc palette title 'send control (src)'

unset multiplot

