#!/bin/bash

if [ "$#" -ne 2 ]; then
	echo "USAGE: $(basename $0) <DIR_TO_PREVIEW> <WWW_SUBDIR>"
	echo " note: the contents of <DIR_TO_PREVIEW> are deployed to:"
	echo "         linux.cs.uwaterloo.ca:public_html/rift/preview/<WWW_SUBDIR>/."
	echo
	echo " using command:"
	echo " rsync -rp <DIR_TO_PREVIEW>/* linux.cs.uwaterloo.ca:public_html/rift/preview/<WWW_SUBDIR>/"
	echo
	exit 1
fi

dir=$1
wwwdir=$2
rsync -rpzvW --progress $1/* linux.cs.uwaterloo.ca:public_html/rift/preview/$wwwdir/
if [ "$?" -eq "0" ]; then
    echo "To preview, open https://cs.uwaterloo.ca/~t35brown/rift/preview/${wwwdir}"
else
	echo "An error has occurred..."
	exit 1
fi
