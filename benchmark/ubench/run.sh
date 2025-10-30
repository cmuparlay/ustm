#!/bin/bash

for m in local random always; do
    for b in small_bench_*; do
        res=$(ITM_DEFAULT_METHOD=ml_wt ./$b -t $(nproc) -i 500000 -s $(nproc) -m $m)
        tp=$(echo -e "$res" | grep Throughput | cut -d' ' -f2)
        echo $m $b $tp
    done
done