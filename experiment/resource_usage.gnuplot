set terminal pngcairo transparent enhanced font "arial,10" fontscale 1.0 size 800, 350
set title "CPU usage (" . peername  .")"
set output plotsdir . "/cpu_usage_" . peername . ".png"

set style fill transparent solid 0.5 noborder
set nokey

set ylabel "Usage (%)"
set xlabel "Time in experiment (s)"

set yrange [0:1]

set xtics 1

plot logdir . "/resource_usage.log.parsed" using 1 with filledcurve x1
