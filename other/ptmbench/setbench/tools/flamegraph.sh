#!/bin/bash

if [ "$#" -eq "0" ]; then
	echo "USAGE: $(basename $0) TITLE_STRING [OPTIONAL ARGS FOR flamegraph.pl]"
	echo " note: this renders the contents of ./perf.data"
	exit 1
fi

title=$1
shift
perf script | ~/FlameGraph/stackcollapse-perf.pl | ~/FlameGraph/flamegraph.pl --bgcolors "#8f8880" --inverted --title "$title" --width 800 $@ > out.svg
echo "Should have created out.svg"
