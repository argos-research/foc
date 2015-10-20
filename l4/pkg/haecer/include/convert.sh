for i in `grep "^\s*perfClass" counters_cpp.h | cut -f2 -d\( | cut -f1 -d\)`; do
	#echo "Events for Group: $i";
	for j in `grep "^\s*perfClass($i) {" -A1000 counters_cpp.h | grep -B1000 -m 1 "};" | grep "^\s*perfEvent"`; do
		event=$(echo $j | sed -e 's/[^(]*(\([^,]*\),\([^)]*\).*/\1_Event = Event(\2);/g');
		evname=$(echo $j | sed -e 's/[^(]*(\([^,]*\).*/\1/g');
		echo "event PEvents_${i}_${event}";	
		for k in `grep "^\s*perfEvent($evname,.*" -A1000 counters_cpp.h | grep -m 1 "END_EVENT" -B1000 | grep "^\s*perfUmask([^)]*)\s*$"`; do
			umask=$(echo $k | sed -e 's/^\s*perfUmask(\([^,]*\),\([^)]*\)).*/\1 = Umask(\2);/g');
			echo "umask PEvents_${i}_${evname}_masks_$umask";
		done;
	done;
done;
