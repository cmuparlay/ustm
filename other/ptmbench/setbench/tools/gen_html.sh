#!/bin/bash

if [ "$#" -ne "2" ]; then
	echo "USAGE: $(basename $0) <IMG_INPUT_DIR> <HTML_OUTPUT_DIR>"
        echo " note: script looks for *.png in <IMG_INPUT_DIR>."
        echo "       for each such file <x.png> found, it looks for optional:"
        echo "           <x.txt> -- to be transfromed into <x.htm>"
        echo "                        and linked as the <click> action on <x.png>"
        echo "           <x.gif> -- to be linked as the <hover> action on <x.png>"
        echo "           <x.mp4> -- to be linked as the <hover> action on <x.mp4>"
        echo "           (only one of <x.gif> or <x.mp4> should exist)"
        echo
        echo "       to get a list of website 'sections' to produce, the script takes"
        echo "       *.png and cuts off the last two '_' separated fields using command"
        echo "           ls *.png | rev | cut -d"_" -f3- | rev | sort | uniq"
        echo
        echo "       for each section, the remaining two '_' separated fields define"
        echo "       rows and columns resp. (each sorted by length then value)."
        echo
        echo "       thus, you define sections, rows and columns by your filenames."
        echo
        echo "       script output/echos logged to <HTML_OUTPUT_DIR>/log_gen.txt"
        echo
	exit 1
fi

indir=$1
outdir=$2

mkdir -p $outdir
cp $indir/* $outdir \
        && cp gen_html/* $outdir \
        && cd $outdir \
        && ./_gen.sh "$(basename $0) $@" "in $(pwd)" >> log_gen.txt
