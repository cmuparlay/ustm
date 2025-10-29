#!/bin/bash

if [ "$#" -le "1" ]; then
	echo "USAGE: $(basename $0) TEXTFILE_NAME COLUMN_SEARCH_STRING [[COLUMN_SEARCH_STRING] ...]"
	echo " note: this tool extracts columnar data from lines of the format 'key=data'"
	exit 1
fi

fname="$1"
shift

#printf "%20s " "$fname"
for ((i=$#;i>0;--i)) ; do
	f="$1"
	shift
	searchstr="^${f}="
#	printf "searchstr=$searchstr\n"
	nlines=`cat $fname | grep -E "$searchstr" | wc -l | tr -d " "`
#	if [ "$nlines" -gt "1" ]; then
#		echo "error: $nlines lines returned for grep query [$searchstr]..."
#		cat $fname | grep "$searchstr"
#		exit 1
#	fi
	val=`cat $fname | grep "$searchstr" | tail -1 | cut -d"=" -f2 | tr -d " "`
	if [ "$nlines" -gt "1" ]; then
		val="$val($nlines)"
	fi
#	printf "%20s " "$val"
	printf "$val"
	if ((i>1)) ; then printf " " ; fi
done
printf "\n"