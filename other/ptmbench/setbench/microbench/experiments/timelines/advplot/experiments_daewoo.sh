#!/bin/bash

## these are experiments for daewoo's thesis (or any associated paper)

## see timeline event rotateEpochBags defined in setbench/common/recordmgr/reclaimer_debra.h
## and TIMELINE_BLIP and TIMELINE_START/END macros in setbench/common/server_clock.h
## and see the GSTATS declarations that actually store and print the timeline/blip files below in setbench/common/recordmgr/gstats_definitions_epochs.h

## TODO: incorporate into graph: algorithm throughput, bag size distribution (over time?) & avg bag size

plotdir=$(pwd)
outdir=$(pwd)/data
compiledir=~/vscode_projects/tmbench/setbench/microbench
rundir=$compiledir/bin

mkdir $outdir 2>/dev/null

for free in immediate amortized ; do

    cd $compiledir
    if [ "$free" == "immediate" ] ; then
        make -j use_timelines=1 debra_orig_free=1 has_libpapi=0
    else
        make -j use_timelines=1 debra_orig_free=0 has_libpapi=0
    fi

    for allocator in jemalloc tcmalloc mimalloc hoard ; do #supermalloc ; do
        for threads in 48 96 192 240 ; do
            for numactl in interleave none ; do
                for pinning in yes no ; do

                    outfile=$outdir/timeline_$allocator_$threads_$numactl_$pinning.txt
                    plotfile=$outdir/timeline_$allocator_$threads_$numactl_$pinning.png

                    cd $rundir

                    if [ "$numactl" == "interleave" ] ; then
                        if [ "$pinning" == "yes" ] ; then
                            LD_PRELOAD=../../lib/lib$allocator.so time numactl --interleave=all ./brown_ext_abtree_lf.debra -nprefill $threads -nwork $threads -insdel 50.0 50.0 -k 20000000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 > $outfile
                        else
                            LD_PRELOAD=../../lib/lib$allocator.so time numactl --interleave=all ./brown_ext_abtree_lf.debra -nprefill $threads -nwork $threads -insdel 50.0 50.0 -k 20000000 -t 5000 > $outfile
                        fi
                    else
                        if [ "$pinning" == "yes" ] ; then
                            LD_PRELOAD=../../lib/lib$allocator.so time ./brown_ext_abtree_lf.debra -nprefill $threads -nwork $threads -insdel 50.0 50.0 -k 20000000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 > $outfile
                        else
                            LD_PRELOAD=../../lib/lib$allocator.so time ./brown_ext_abtree_lf.debra -nprefill $threads -nwork $threads -insdel 50.0 50.0 -k 20000000 -t 5000 > $outfile
                        fi
                    fi

                    paste -d " " - - - < timeline_rotateEpochBags.txt | awk '{print "rotateEpochBags",$1,$3,$6,$9}' > timeline_rotateEpochBags_processed.txt \
                    && paste -d " " - - < blip_advanceEpoch.txt | awk '{print "blip_advanceEpoch",$1,$3,$6}' > timeline_blip_advanceEpoch_processed.txt \
                    && cat timeline_blip_advanceEpoch_processed.txt timeline_rotateEpochBags_processed.txt > timeline_data.txt \
                    && cp timeline_data.txt $outdir/

                    cd $plotdir
                    python ./timeline_advplot.py $outdir/timeline_data.txt $plotfile rotateEpochBags sequence blip_advanceEpoch yellow

                done
            done
        done
    done

done
