#!/bin/bash

if [ -z "$1" ]; then
    echo "Arg1 must be the txt file containing the file names"
	exit -1
fi

if [ -z "$2" ]; then
    echo "Arg2 must be the name of direcotry where we create the new files"
	exit -1
fi

files=`cat $1`
data_dir=$2
for f in $files;
do
	echo $f
	rm -f $data_dir/$f
	echo "" > $data_dir/$f
done