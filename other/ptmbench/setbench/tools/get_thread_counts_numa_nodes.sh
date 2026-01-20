#!/bin/bash

export PATH=$PATH:.
echo `get_lscpu_numa_nodes.sh | awk '{row[NR]=(NR > 1 ? row[NR-1] : 0)+NF} END { row[NR]-=2 ; row[NR+1]=row[1]/2 ; for (i in row) print row[i] }' | sort -n`
