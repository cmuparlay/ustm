#!/bin/bash

# cd bin
# echo "" | tee testRunFGL_sorting.txt
# echo "" | tee testRunFGL_sorting_strong_prog.txt
# for ((i=0;i<100;i++)) ; do
# 	echo $i | tee -a testRunFGL_sorting.txt
# 	echo $i | tee -a testRunFGL_sorting_strong_prog.txt
# 	for ((t=8;t<96;t+=8)) ; do		
# 		# ./bin/brown_abtree_tm_auto.none.hytm3 -nwork 95 -nprefill 1 -i 25 -d 25 -k 1000000 -t 20000 -pin 0-23,48-71,24-47,72-95 | grep "TOO MANY"	
# 		timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_wrset_sorting -nwork $t -prefill-insert -nprefill 1 -insdel 50.0 50.0 -k 100000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 | tee -a testRunFGL_sorting.txt
# 		timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork $t -prefill-insert -nprefill 1 -insdel 50.0 50.0 -k 100000 -t 5000 -pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191 | tee -a testRunFGL_sorting_strong_prog.txt
# 	done
# done

file1=testRun_phytm1.txt
file2=testRun_phytm1_always_sorting_strong_prog.txt
k=1000000
t=10000
ins1=50.0
ins2=5.0
del1=50.0
del2=5.0


# single run
cd bin
echo "" | tee $file1
echo "" | tee $file2
for ((i=0;i<5;i++)) ; do
	echo $i | tee -a $file1
	echo $i | tee -a $file2
	for ((t=35;t<=95;t+=10)) ; do		
		# ./bin/brown_abtree_tm_auto.none.hytm3 -nwork 95 -nprefill 1 -i 25 -d 25 -k 1000000 -t 20000 -pin 0-23,48-71,24-47,72-95 | grep "TOO MANY"	
		timeout 360 numactl --interleave=all time ./abtree.none.phytm1 -nwork $t -prefill-insert -nprefill 1 -insdel $ins1 $del1 -k $k -t $t -pin 0-23,48-71,24-47,72-95 | tee -a $file1
		timeout 360 numactl --interleave=all time ./abtree.none.phytm1 -nwork $t -prefill-insert -nprefill 1 -insdel $ins2 $del2 -k $k -t $t -pin 0-23,48-71,24-47,72-95 | tee -a $file1
		timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork $t -prefill-insert -nprefill 1 -insdel $ins1 $del1 -k $k -t $t -pin 0-23,48-71,24-47,72-95 | tee -a $file2
		timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork $t -prefill-insert -nprefill 1 -insdel $ins2 $del2 -k $k -t $t -pin 0-23,48-71,24-47,72-95 | tee -a $file2
	done
done

# timeout 360 numactl --interleave=all time ./abtree.none.phytm1_fgl_always_sorting_strong_progressive -nwork 95 -prefill-insert -nprefill 1 -insdel 50.0 50.0 -k 100000 -t 5000 -pin 0-23,48-71,24-47,72-95 | tee -a testRunFGL_sorting.txt
