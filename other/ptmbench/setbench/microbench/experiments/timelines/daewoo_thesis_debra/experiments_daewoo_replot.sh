#!/bin/bash

plotdir=$(pwd)
outdir=$(pwd)/data
compiledir=~/vscode_projects/tmbench/setbench/microbench
rundir=$compiledir/bin

mkdir $outdir 2>/dev/null

## note: if we see an anomalous column, rerun with perf record, then reidentify an anomalous column, and then perf report twice filtered to anomalous and non anomalous ranges to compare...

## add path to tools to the path env
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export PATH=$SCRIPTPATH/../../../../tools:$SCRIPTPATH:$PATH

for free in immediate ; do
    for allocator in jemalloc tcmalloc mimalloc ; do
        for threads in 48 ; do ## 192 240
            for numactl in interleave ; do
                for pinning in yes ; do
                    if [ "$threads" == "240" ] ; then pinning=no ; fi ## cannot pin when oversubscribing (with current implementation of setbench anyway)

                    # if [ "$free" == "amortized" ] && [ "$"]

                    common_file_infix=${free}_${allocator}_${threads}_${numactl}_pin${pinning}
                    outfile=$outdir/freetime_${common_file_infix}.txt
                    timelinedata=$outdir/freetime_tl_${common_file_infix}.txt
                    timelinezip=$outdir/freetime_tl_${common_file_infix}.zip
                    plotfile=$outdir/freetime_${common_file_infix}.png
                    stripfile=$outdir/unreclaimed_${common_file_infix}.png

                    ## prepare command line arguments

                    # common="./brown_ext_abtree_lf.debra -insdel 50.0 50.0 -k 200000 -nprefill $threads -nwork $threads -t 100" ###### TESTING
                    common="./brown_ext_abtree_lf.debra -insdel 50.0 50.0 -k 20000000 -nwork $threads -t 5000"

                    command_perf="perf record -F 999 --call-graph=lbr --clockid=CLOCK_MONOTONIC"
                    # command_perf=""

                    command_numactl=""
                    if [ "$numactl" == "interleave" ] ; then
                        command_numactl="numactl --interleave=all"
                    fi

                    command_pinning=""
                    if [ "$pinning" == "yes" ] ; then
                        command_pinning="-pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191"
                    fi

                    # ## run
                    # cd $rundir

                    # ## perf report to determine how much time was spent in free()
                    # stime=$(grep REALTIME_START_PERF_FORMAT $outfile | cut -d"=" -f2) ## during the measured interval specifically...
                    # ftime=$(grep REALTIME_END_PERF_FORMAT $outfile | cut -d"=" -f2)
                    # # perf report --time $stime,$ftime --stdio --call-graph=folded >> $outfile

                    ## only make timelines for immediate free, and a small subset of amortized free options
                    if [ "$free" != "amortized" ] || ( [ "$numactl" == "interleave" ] && ( ( [ "$threads" == "240" ] && [ "$pinning" == "no" ] ) || [ "$threads" == "192" ] ) ) ; then

                        # ## prepare timeline data
                        # paste -d " " - - - < timeline_rotateEpochBags.txt | awk '{print "rotateEpochBags",$1,$3,$6,$9}' > timeline_rotateEpochBags_processed.txt \
                        #     && paste -d " " - - < blip_advanceEpoch.txt | awk '{print "blip_advanceEpoch",$1,$3,$6}' > timeline_blip_advanceEpoch_processed.txt \
                        #     && cat timeline_blip_advanceEpoch_processed.txt timeline_rotateEpochBags_processed.txt > $timelinedata

                        # ## quick sanity check on the data
                        # rows_firstfile=$(cat timeline_rotateEpochBags.txt | wc -l)
                        # rows_secondfile=$(cat blip_advanceEpoch.txt | wc -l)
                        # if (((rows_firstfile % 3) != 0 || (rows_secondfile % 2) != 0)) ; then
                        #     echo "ERROR: rows in timeline_rotateEpochBags.txt not a multiple of 3, or in blip_advanceEpoch.txt not a multiple of 2! it's likely you hit the size limits of the GSTATS variable(s) used to track these timeline intervals / blips. see gstats_definitions.epochs.h."
                        #     exit 1
                        # fi

                        ##
                        ## prepare title
                        ##

                        ## percent time in free
                        # if [ "$allocator" == "jemalloc" ] ; then
                        #     freepcnt=$(grep '\.] free' $outfile | tr -d " " | cut -d"%" -f1)
                        # elif [ "$allocator" == "mimalloc" ] ; then
                        #     freepcnt=$(grep '\.] mi_free_generic' $outfile | tr -d " " | cut -d"%" -f1)
                        # elif [ "$allocator" == "hoard" ] ; then
                        #     freepcnt=$(grep 'BigHeap>::free$' $outfile | tr -d " " | cut -d"%" -f1)
                        # fi

                        freepcnt_str=""
                        # if [ "$allocator" != "tcmalloc" ] ; then
                        #     freepcnt_str=",  time spent freeing=${freepcnt}%"
                        # fi

                        ## throughput in millions
                        # tput=$(grep "total_throughput=" $outfile | cut -d"=" -f2)
                        # tputmil=$(echo "scale=1; $tput / 1000000" | bc)
                        # suptitle="$allocator, free=$free, numactl=$numactl, pinning=$pinning, threads=$threads"
                        # title="throughput=${tputmil}Mops/sec${freepcnt_str}"
                        suptitle=""
                        title=""

                        ## plot timeline
                        cd $plotdir

                        ## assuming timeline data is zipped for space reasons... (delete after)
                        cd data
                        unzip -oj $timelinezip
                        cd ..

                        python ./timeline_advplot_light.py $timelinedata $plotfile "$suptitle" "$title" rotateEpochBags sequence blip_advanceEpoch blue
                        imgcatr $plotfile

                        # ## zip timeline_data file (to preserve it without occupying too much space)
                        # zip $timelinezip $timelinedata
                        rm $timelinedata

                        # exit 1
                    fi

                    ## plot supporting graph strip
                    line=$(cat "$outfile" | grep average_garbage_in_epoch_by_index | tail -1)
                    echo "$line" | cut -d"=" -f2 | tr " " "\n" | awk '{print NR, $1}' \
                        | plotline.py -o $stripfile --scalefactor $threads --fontsize=22 --heightinches=3.75 --x-title "epoch number" --y-title "garbage nodes" --lightmode --trim-prefix-zeros
                    imgcatr $stripfile

                done
            done
        done
    done
done
