#!/bin/bash

## see timeline event rotateEpochBags defined in setbench/common/recordmgr/reclaimer_debra.h
## and TIMELINE_BLIP and TIMELINE_START/END macros in setbench/common/server_clock.h
## and see the GSTATS declarations that actually store and print the timeline/blip files below in setbench/common/recordmgr/gstats_definitions_epochs.h

# && LD_PRELOAD=../../lib/libjemalloc.so time numactl --interleave=all ./brown_ext_abtree_lf.debra -nprefill 192 -nwork 192 -insdel 50.0 50.0 -k 20000000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 -rq 0 -rqsize 1 -nrq 0 \

## this first approach assumes you're using TIMELINE_..._INMEM_... macros (as opposed to non-INMEM macros)

mydir=$(pwd)
cd ~/vscode_projects/tmbench/setbench/microbench \
&& make -j use_timelines=1 debra_orig_free=0 has_libpapi=0 \
&& cd bin \
&& LD_PRELOAD=../../lib/libmimalloc.so time ./brown_ext_abtree_lf.debra -nprefill 240 -nwork 240 -insdel 50.0 50.0 -k 20000000 -t 5000 \
| tee temp.txt \
&& paste -d " " - - - < timeline_rotateEpochBags.txt | awk '{print "rotateEpochBags",$1,$3,$6,$9}' > timeline_rotateEpochBags_processed.txt \
&& paste -d " " - - < blip_advanceEpoch.txt | awk '{print "blip_advanceEpoch",$1,$3,$6}' > timeline_blip_advanceEpoch_processed.txt \
&& cat timeline_blip_advanceEpoch_processed.txt timeline_rotateEpochBags_processed.txt > timeline_data.txt \
&& (cp timeline_data.txt ../experiments/timelines/advplot/ ; cp temp.txt ../experiments/timelines/advplot/) \
&& cd $mydir \
&& python ./timeline_advplot.py timeline_data.txt temp.png rotateEpochBags sequence blip_advanceEpoch yellow


## note: the following approach uses prep_data.sh to do some grepping / sed regex replacing to clean up the output of setbench,
## in case you're using TIMELINE macros instead of TIMELINE_INMEM macros.
## this does NOT need to use run_advplot.sh. indeed, this is an alternative to run_advplot.sh.

# && LD_PRELOAD=../../lib/libjemalloc.so time ./brown_ext_abtree_lf.debra -nprefill 192 -nwork 192 -insdel 50.0 50.0 -k 20000000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 -rq 0 -rqsize 1 -nrq 0 \
# | tee temp.txt | grep -v timeline \
# && cp temp.txt ../experiments/timelines/advplot/temp_unfiltered.txt
# && ./prep_data.sh temp_unfiltered.txt temp_filtered.txt rotateEpochBags sequence \
# && sort -n -k1,2 temp_filtered.txt > temp_sorted.txt \
# && python ./timeline_advplot.py temp_sorted.txt temp.png rotateEpochBags sequence


# bash -c "cd ~/vscode_projects/tmbench/setbench/microbench && make -j use_timelines=1 debra_orig_free=1 has_libpapi=0 && cd bin && LD_PRELOAD=../../lib/libjemalloc.so time ./brown_ext_abtree_lf.debra -nprefill 192 -nwork 192 -insdel 50.0 50.0 -k 20000000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 -rq 0 -rqsize 1 -nrq 0 > temp.txt && paste -d " " - - - < timeline_rotateEpochBags.txt | awk '{print $1,$3,$6,$9}' > timeline_rotateEpochBags_processed.txt && (cp timeline*.txt ../experiments/timelines/advplot/ ; cp temp.txt ../experiments/timelines/advplot/temp_unfiltered.txt)"
