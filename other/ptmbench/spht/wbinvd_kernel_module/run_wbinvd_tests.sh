#!/bin/bash

thread_counts="1, 2, 4, 8, 16, 32, 48, 64, 72, 80, 96"
millis_to_run="20000"


echo "" > WBINVD_disabled.txt
echo "" > WBINVD_enabled.txt

g++ numaTest.cpp -I../.. -lpthread -lpapi -DUSE_PAPI -DDISABLE_WBINVD

for n in $thread_counts ; do
	for t in $millis_to_run ; do
		./a.out -t $t -n $n -pin 0-23,48-71,24-47,72-95 >> WBINVD_disabled.txt
		echo "" >> WBINVD_disabled.txt
	done
done

g++ numaTest.cpp -I../.. -lpthread -lpapi -DUSE_PAPI

for n in $thread_counts ; do
	for t in $millis_to_run ; do
		./a.out -t $t -n $n -pin 0-23,48-71,24-47,72-95 >> WBINVD_enabled.txt
		echo "" >> WBINVD_enabled.txt
	done
done

