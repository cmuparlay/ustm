#!/bin/bash

# export PATH=$PATH:.
# get_lscpu_numa_nodes.sh | tr "\n" " " | tr " " "," | rev | cut -d"," -f2- | rev

lscpu | grep -E "NUMA node[0-9]+ CPU" | cut -d":" -f2 | tr -d " " | tr "\n" "," | rev  | cut -d"," -f2- | rev
