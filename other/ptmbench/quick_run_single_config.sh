#!/bin/bash

file1=quickRun.txt

cd bin
echo "" | tee $file1
for ((i=0;i<500;i++)) ; do
	echo $i | tee -a $file1	
	timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork 96 -nprefill 96 -insdel 50.0 50.0 -k 100000 -t 5000 -pin 0-23,48-71,24-47,72-95 | tee -a $file1	
done

# timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork 95 -prefill-insert -nprefill 1 -insdel 50.0 50.0 -k 100000 -t 5000 -pin 0-23,48-71,24-47,72-95 | tee -a testRunFGL_sorting.txt

# assembly generation
# g++-10 -S setbench/microbench/main.cpp -o /home/gccoccim/research/nvram_research/ptmbench/experiments_phytm/../bin/abtree.none.phytm1_fgl.asm -DUSE_GSTATS_USER_HANDLER_H_FILE -Iphytmlib -Ids_phytm/abtree -DDS_TYPENAME=abtree -DRECLAIM_TYPE=none -Dphytm1_fgl  -DUSE_LIBNUMA -DNDEBUG -DDISABLE_PHYTM_BACKOFF -DDISABLE_ZEROING_MEMORY -O2 -mcx16  -DMAX_THREADS_POW2=128 -DCPU_FREQ_GHZ=2.1  -DMEMORY_STATS=if\(0\) -DMEMORY_STATS2=if\(0\) -DNO_CLEANUP_AFTER_WORKLOAD  -DRECORD_ABORTS -DGSTATS_MAX_THREAD_BUF_SIZE=524288 -DDEBRA_ORIGINAL_FREE   -std=c++17 -g  -lnuma -Lsetbench/lib -I. -Isetbench/microbench `find setbench/common -type d | sed s/^/-I/` -lpthread -latomic -ldl -mrtm
