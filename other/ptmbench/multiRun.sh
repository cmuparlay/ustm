#!/bin/bash

pinning="-pin", "0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191"

until timeout 360 numactl --interleave=all time ./bin/abtree.none.trinity_quadra -nwork 72 -nprefill 72 -insdel 50.0 50.0 -k 1000000 -t 20000 -pin "$pinning"; [ $? -eq 139 ]; do printf 'No segfault\n'; done

