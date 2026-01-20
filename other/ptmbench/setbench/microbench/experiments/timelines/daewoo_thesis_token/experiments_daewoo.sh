#!/bin/bash

## these are experiments for daewoo kim's thesis (or any associated paper)
## specifically, these are for timeline & garbage plots for token EBR variants

## see ../daewoo_thesis_debra/*.sh for more comments & details

plotdir=$(pwd)
outdir=$(pwd)/data
compiledir=~/vscode_projects/tmbench/setbench/microbench
rundir=$compiledir/bin

mkdir $outdir 2>/dev/null

## add path to tools to the path env
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export PATH=$SCRIPTPATH/../../../../tools:$SCRIPTPATH:$PATH

##
## compile experiments
##

## setting debra_orig_free=0 --> does not -DDEBRA_ORIG_FREE --> allows reclaimer_token[1-4].h to define it as appropriate for these experiments
cd $compiledir
echo "compiling with: make -j RECLAIMERS='token1 token2 token3 token4' debra_orig_free=0 use_timelines=1 xargs='-D___MIN_INTERVAL_DURATION=0.1' has_libpapi=0"
make -j debug_timeline_record_every_deamortized_free=1 RECLAIMERS='token1 token2 token3 token4' debra_orig_free=0 use_timelines=1 xargs='-D___MIN_INTERVAL_DURATION=0.1' has_libpapi=0
if [ "$?" -ne "0" ]; then
    echo "ERROR COMPILING"
    exit 1
fi

##
## run experiments across all parameter combinations
##

# for reclaimer in token3 token1 token2 token4 ; do
#     for allocator in jemalloc mimalloc ; do
#         for threads in 240 192 96 48 ; do
#             for numactl in interleave ; do
#                 for pinning in yes ; do
for reclaimer in token4 ; do
    for allocator in jemalloc ; do
        for threads in 192 ; do
            for numactl in interleave ; do
                for pinning in yes ; do
                    if [ "$threads" == "240" ] ; then pinning=no ; fi ## cannot pin when oversubscribing (with current implementation of setbench anyway)

                    common_file_infix=${reclaimer}_${allocator}_${threads}_${numactl}_pin${pinning}
                    outfile=$outdir/freetime_${common_file_infix}.txt
                    timelinedata=$outdir/freetime_tl_${common_file_infix}.txt
                    timelinedatatwo=$outdir/freeOne_tl_${common_file_infix}.txt
                    timelinezip=$outdir/freetime_tl_${common_file_infix}.zip
                    plotfile=$outdir/freetime_${common_file_infix}.png
                    plotfiletwo=$outdir/freeOne_${common_file_infix}.png
                    stripfile=$outdir/unreclaimed_${common_file_infix}.png

                    ## prepare command line arguments

                    # common="./brown_ext_abtree_lf.${reclaimer} -insdel 50.0 50.0 -k 20000 -nprefill $threads -nwork $threads -t 100" ###### TESTING
                    common="./brown_ext_abtree_lf.${reclaimer} -insdel 50.0 50.0 -k 20000000 -nprefill $threads -nwork $threads -t 5000"

                    command_perf=""
                    # command_perf="perf record -F 999 --call-graph=lbr --clockid=CLOCK_MONOTONIC"

                    command_numactl=""
                    if [ "$numactl" == "interleave" ] ; then
                        command_numactl="numactl --interleave=all"
                    fi

                    command_pinning=""
                    if [ "$pinning" == "yes" ] ; then
                        command_pinning="-pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191"
                    fi

                    ## run
                    cd $rundir
                    LD_PRELOAD=../../lib/lib$allocator.so time $command_perf $command_numactl $common $command_pinning > $outfile 2>&1
                    echo "LD_PRELOAD=../../lib/lib$allocator.so time $command_perf $command_numactl $common $command_pinning > $outfile 2>&1" tee -a $outfile ## print command to file

                    ## perf report to determine how much time was spent in free()
                    stime=$(grep REALTIME_START_PERF_FORMAT $outfile | cut -d"=" -f2) ## during the measured interval specifically...
                    ftime=$(grep REALTIME_END_PERF_FORMAT $outfile | cut -d"=" -f2)
                    # perf report --time $stime,$ftime --stdio --call-graph=folded >> $outfile



                    ## prepare timeline data
                    paste -d " " - - - < timeline_rotateEpochBags.txt | awk '{print "rotateEpochBags",$1,$3,$6,$9}' > timeline_rotateEpochBags_processed.txt \
                        && paste -d " " - - < blip_advanceEpoch.txt | awk '{print "blip_advanceEpoch",$1,$3,$6}' > timeline_blip_advanceEpoch_processed.txt \
                        && cat timeline_blip_advanceEpoch_processed.txt timeline_rotateEpochBags_processed.txt > $timelinedata

                    ## quick sanity check on the data
                    rows_firstfile=$(cat timeline_rotateEpochBags.txt | wc -l)
                    rows_secondfile=$(cat blip_advanceEpoch.txt | wc -l)
                    if (((rows_firstfile % 3) != 0 || (rows_secondfile % 2) != 0)) ; then
                        echo "ERROR: rows in timeline_rotateEpochBags.txt not a multiple of 3, or in blip_advanceEpoch.txt not a multiple of 2! it's likely you hit the size limits of the GSTATS variable(s) used to track these timeline intervals / blips. see gstats_definitions.epochs.h."
                        exit 1
                    fi



                    ## prepare freeOne timeline data (relies on above blip_advanceEpoch processing)
                    paste -d " " - - - < timeline_freeOne.txt | awk '{print "freeOne",$1,$3,$6,$9}' > timeline_freeOne_processed.txt \
                        && cat timeline_blip_advanceEpoch_processed.txt timeline_freeOne_processed.txt > $timelinedatatwo

                    ## quick sanity check on the data
                    rows_firstfile=$(cat timeline_freeOne.txt | wc -l)
                    rows_secondfile=$(cat blip_advanceEpoch.txt | wc -l)
                    if (((rows_firstfile % 3) != 0 || (rows_secondfile % 2) != 0)) ; then
                        echo "ERROR: rows in timeline_freeOne.txt not a multiple of 3, or in blip_advanceEpoch.txt not a multiple of 2! it's likely you hit the size limits of the GSTATS variable(s) used to track these timeline intervals / blips. see gstats_definitions.epochs.h."
                        exit 1
                    fi



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
                    # echo "python ./timeline_advplot_light.py $timelinedata $plotfile "$suptitle" "$title" rotateEpochBags sequence blip_advanceEpoch blue" | tee -a $outfile
                    # python ./timeline_advplot_light.py $timelinedata $plotfile "$suptitle" "$title" rotateEpochBags sequence blip_advanceEpoch blue
                    python ./timeline_advplot_light.py $timelinedatatwo $plotfiletwo "$suptitle" "$title" freeOne sequence blip_advanceEpoch blue

                    ## zip timeline_data file (to preserve it without occupying too much space)
                    zip $timelinezip $timelinedata $timelinedatatwo
                    rm $timelinedata $timelinedatatwo

                    # ## plot supporting graph strip
                    # line=$(cat "$outfile" | grep average_garbage_in_epoch_by_index | tail -1)
                    # echo "$line" | cut -d"=" -f2 | tr " " "\n" | awk '{print NR, $1}' \
                    #     | plotline.py -o $stripfile  --scalefactor 192 --fontsize=22 --heightinches=3.75 --x-title "epoch number" --y-title "garbage nodes" --lightmode --trim-prefix-zeros

                    ## ## plot supporting graph strip
                    ## line=$(cat "$outfile" | grep average_garbage_in_epoch_by_index | tail -1)
                    ## echo "$line" | cut -d"=" -f2 | tr " " "\n" | awk '{print NR, $1}' \
                    ##     | plotline.py -o $stripfile -t "$allocator,  pinning=$pinning,  threads=$threads" --suptitle "average number of unreclaimed objects per thread" --x-title "epoch number" --lightmode --trim-prefix-zeros

                done
            done
        done
    done
done
