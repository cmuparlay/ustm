#!/bin/bash

for f in $(ls data/free*.txt) ; do
    tput=$(grep "total_throughput=" $f | cut -d"=" -f2)
    tputmil=$(echo "scale=1; $tput / 1000000" | bc)
    epochs=$(grep "global_epoch_counter=" $f | tail -1 | cut -d"=" -f2)
    reclamationevents=$(grep "sum_limbo_reclamation_event_count_total" $f | tail -1 | cut -d"=" -f2)
    frees=$(grep "limbo_object_frees" $f | cut -d"=" -f2)

    allocator=$(echo $f | cut -d"_" -f3)

    # percent time in free
    freepcnt="0"
    if [ "$allocator" == "jemalloc" ] ; then
        freepcnt=$(grep '\[\.\] free' $f | tr -d " " | cut -d"%" -f1)
    elif [ "$allocator" == "mimalloc" ] ; then
        freepcnt=$(grep '\[\.\] mi_free_generic' $f | tr -d " " | cut -d"%" -f1)
    elif [ "$allocator" == "hoard" ] ; then
        freepcnt=$(grep 'BigHeap>::free\$' $f | tr -d " " | cut -d"%" -f1)
    elif [ "$allocator" == "tcmalloc" ] ; then
        freepcnt=$(grep '\[\.\] syscall@plt' $f | tr -d " " | cut -d"%" -f1)
    fi

    flushpcnt=$(grep '\[\.\] je_tcache_bin_flush_small' $f | tr -d " " | cut -d"%" -f1)

    # echo "epochs=$epochs"
    # echo "frees=$frees"
    (echo -n "$f" | cut -d"/" -f2 | cut -d"." -f1 | tr "_" "," | cut -d"," -f2- | tr -d "\n" ; echo ",$tputmil,$freepcnt,$epochs,$reclamationevents,$frees,$flushpcnt") | grep -v none
done