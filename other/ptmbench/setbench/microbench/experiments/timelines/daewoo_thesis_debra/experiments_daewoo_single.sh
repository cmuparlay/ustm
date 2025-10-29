#!/bin/bash

## these are experiments for daewoo kim's thesis (or any associated paper)

## see timeline event rotateEpochBags defined in setbench/common/recordmgr/reclaimer_debra.h
## and TIMELINE_BLIP and TIMELINE_START/END macros in setbench/common/server_clock.h
## and see the GSTATS declarations that actually store and print the timeline/blip files below in setbench/common/recordmgr/gstats_definitions_epochs.h

#
# note: for computing %-time-spent-in-free(), the following perf symbols seem useful...
#
# tcmalloc
#     [.] tcmalloc::ThreadCache::ReleaseToCentralCache
#     [.] tcmalloc::ThreadCache::Scavenge
#     syscall@plt
#     native_queued_spin_lock_slowpath
#
#     really unclear how to quantify well..
#     maybe need to figure out perf script myself to manually tally samples coming from the libtcmalloc.so dso, and possibly from relevant kernel locks.
#     a problem for later... for a paper maybe...
#
# jemalloc
#     [.] free
#
# mimalloc
#     [.] mi_free_generic
#
# hoard
#     "BigHeap>::free$"
#

plotdir=$(pwd)
outdir=$(pwd)/data
compiledir=~/vscode_projects/tmbench/setbench/microbench
rundir=$compiledir/bin

mkdir $outdir 2>/dev/null

## note: if we see an anomalous column, rerun with perf record, then reidentify an anomalous column, and then perf report twice filtered to anomalous and non anomalous ranges to compare...

## add path to tools to the path env
SCRIPTPATH="$( cd "$(dirname "$0")" >/dev/null 2>&1 ; pwd -P )"
export PATH=$SCRIPTPATH/../../../../tools:$SCRIPTPATH:$PATH

## run experiments across all parameter combinations
for free in amortized ; do

    cd $compiledir
    if [ "$free" == "immediate" ] ; then
        make -j use_timelines=1 debra_orig_free=1 has_libpapi=0
        if [ "$?" -ne "0" ]; then
            echo "ERROR COMPILING"
            exit 1
        fi
    else
        make -j debug_timeline_record_every_deamortized_free=1 use_timelines=1 debra_orig_free=0 has_libpapi=0
        if [ "$?" -ne "0" ]; then
            echo "ERROR COMPILING"
            exit 1
        fi
    fi

    for allocator in jemalloc ; do
        for threads in 192 ; do
            for numactl in interleave ; do
                for pinning in yes ; do
                    if [ "$threads" == "240" ] ; then pinning=no ; fi ## cannot pin when oversubscribing (with current implementation of setbench anyway)

                    # if [ "$free" == "amortized" ] && [ "$"]

                    common_file_infix=${free}_${allocator}_${threads}_${numactl}_pin${pinning}
                    outfile=$outdir/freetime_${common_file_infix}.txt
                    timelinedata=$outdir/freetime_tl_${common_file_infix}.txt
                    timelinezip=$outdir/freetime_tl_${common_file_infix}.zip
                    timelinedatatwo=$outdir/freeOne_tl_${common_file_infix}.txt
                    plotfile=$outdir/freetime_${common_file_infix}.png
                    plotfiletwo=$outdir/freeOne_${common_file_infix}.png
                    stripfile=$outdir/unreclaimed_${common_file_infix}.png

                    ## prepare command line arguments

                    # common="./brown_ext_abtree_lf.debra -insdel 50.0 50.0 -k 200000 -nprefill $threads -nwork $threads -t 100" ###### TESTING
                    common="./brown_ext_abtree_lf.debra -insdel 50.0 50.0 -k 20000000 -nwork $threads -t 5000"

                    command_perf=""
                    # command_perf="perf record -F 999 -e task-clock --call-graph=lbr --clockid=CLOCK_MONOTONIC"
                    command_perf="perf stat -e task-clock -I 10"

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

                    ## only make timelines for immediate free, and a small subset of amortized free options
                    if [ "$free" != "amortized" ] || ( ( [ "$threads" == "240" ] && [ "$pinning" == "no" ] || [ "$threads" == "192" ] ) ) ; then
                    # if [ "$free" != "amortized" ] || ( [ "$numactl" == "interleave" ] && ( ( [ "$threads" == "240" ] && [ "$pinning" == "no" ] ) || [ "$threads" == "192" ] ) ) ; then



                        ## prepare rotateEpochBags timeline data
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

                        ## plot timelines
                        cd $plotdir
                        # python ./timeline_advplot_light.py $timelinedata $plotfile "$suptitle" "$title" rotateEpochBags sequence blip_advanceEpoch blue
                        python ./timeline_advplot_light.py $timelinedatatwo $plotfiletwo "$suptitle" "$title" freeOne sequence blip_advanceEpoch blue

                        ## zip timeline_data file (to preserve it without occupying too much space)
                        zip $timelinezip $timelinedata $timelinedatatwo
                        rm $timelinedata $timelinedatatwo

                        # exit 1
                    fi

                    # ## plot supporting graph strip
                    # line=$(cat "$outfile" | grep average_garbage_in_epoch_by_index | tail -1)
                    # echo "$line" | cut -d"=" -f2 | tr " " "\n" | awk '{print NR, $1}' \
                    #     | plotline.py -o $stripfile --scalefactor $threads --fontsize=22 --heightinches=3.75 --x-title "epoch number" --y-title "garbage nodes" --lightmode --trim-prefix-zeros

                done
            done
        done
    done
done

                    # ../../../../microbench_experiments/trial_to_plot/trial_to_plotline_light.sh $outfile average_garbage_in_epoch_by_index \
                    # && mv out.png $stripfile

                    # mydir=$(pwd) && cd ../../.. && make -j use_timelines=1 debra_orig_free=1 has_libpapi=0 && cd bin && LD_PRELOAD=../../lib/libjemalloc.so perf record -F 999 --call-graph=lbr --clockid=CLOCK_MONOTONIC numactl --interleave=all time ./brown_ext_abtree_lf.debra -insdel 50.0 50.0 -k 20000000 -nprefill 96 -nwork 96 -t 5000 > temp_perf.txt 2>&1 && cat temp_perf.txt && stime=$(grep REALTIME_START_PERF_FORMAT temp_perf.txt | cut -d"=" -f2) && ftime=$(grep REALTIME_END_PERF_FORMAT temp_perf.txt | cut -d"=" -f2) && echo && perf report --time $stime,$ftime --stdio -n -g folded | grep 'free' | grep -v 'abtree' 2>&1 | tee temp_perf2.txt && freepcnt=$(grep '] free' temp_perf2.txt | tr -d " " | cut -d"%" -f1) && echo $freepcnt ; cd $mydir
                    # mydir=$(pwd) ; cd ../../../bin ; perf report ; cd $mydir
                    # mydir=$(pwd) ; cd ../../../bin ; perf report --time $(grep REALTIME_START_PERF_FORMAT temp_perf2.txt | cut -d"=" -f2),$(grep REALTIME_END_PERF_FORMAT temp_perf2.txt | cut -d"=" -f2) --stdio -n -g folded | grep malloc ; cd $mydir
