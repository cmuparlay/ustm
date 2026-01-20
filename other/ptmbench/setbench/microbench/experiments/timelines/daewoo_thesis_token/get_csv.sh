#!/bin/bash

for f in $(ls data/free*.txt) ; do
    tput=$(grep "total_throughput=" $f | cut -d"=" -f2)
    tputmil=$(echo "scale=1; $tput / 1000000" | bc)
    epochs=$(grep "global_epoch_counter=" $f | tail -1 | cut -d"=" -f2)
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

    # echo "epochs=$epochs"
    # echo "frees=$frees"
    (echo -n "$f" | cut -d"/" -f2 | cut -d"." -f1 | tr "_" "," | cut -d"," -f2- | tr -d "\n" ; echo ",$tputmil,$freepcnt,$epochs,$frees") | grep -v none
done


## typical usage:
# ./get_csv.sh 2>/dev/null | grep -vE "supermalloc|hoard" | tr "," "\t" | grep -E "240" | grep pin | sort -s -k1,2
