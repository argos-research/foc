set autoscale
unset log
unset label
set xtic auto
set ytic nomirror auto
set y2tic auto
set title "Corelation between TSC and read energy counter"
set xlabel 'Update No'
set ylabel 'Time between Updates (in TSC)'
set y2label 'Deviation from average'
set yrange [-2500000:2500000]
set y2range [-0.0471588373:0.0420450163]
set xrange [0:40960]
#average=2499892
#set terminal postscript
#set output "plot.ps"
plot 'tsc.dat' using 5 axes x1y1
#! lp -d OSPrinter -ops plot.ps
