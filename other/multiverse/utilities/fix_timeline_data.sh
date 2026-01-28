#!/bin/bash

if [ -z "$1" ]; then
    echo "Must supply the input file"
fi

if [ ! -f $1 ]; then
    echo "Error, input file not found"
fi

if [ -z "$2" ]; then
    echo "Must supply the output file name"
fi

eventName=""
if [ -z "$3" ]; then
    echo "No event name given, we will use the filename (excluding extension)"		
	eventName="${1%.*}"	
else
	eventName="$3"
fi

echo "Processing event: $eventName" 

# This assumes your timeline input file contains lines that form pairs of start/end times
# 	for a single event type such as the following:
# 	0 0 11714128008499780
# 	0 1 11714128010316304
#   These lines have the format:
#   tid counter time
#   Where counter being even refers to a start time and odd refers to an end time

# 	This means that after formatting with paste
#   the start time is the 3rd column and end time is the 6th column
paste -d " " - - < $1 | awk -v prefix="$eventName" '{print prefix,$1,$3,$6}' > $2