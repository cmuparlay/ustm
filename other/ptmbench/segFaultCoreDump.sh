#!/bin/bash

ulimit -c unlimited

yes | mv core core_old

# ./compile_phytm.sh
# ./compile_trinity_quadra.sh

until timeout 360 numactl --interleave=all time ./bin/abtree.none.trinity_quadra -nwork 72 -nprefill 72 -insdel 50.0 50.0 -k 1000000 -t 20000 -pin 0-23,48-71,24-47,72-95; [ $? -eq 139 ]; do printf 'No segfault\n'; done