#!/bin/bash

# hwthreads=`lscpu | grep -E "^CPU\(s\):\s+[0-9]+$" | tr -d "a-zA-Z(): "`
# echo $((hwthreads - 2))

export PATH=$PATH:.
echo $(get_thread_counts_numa_nodes.sh | rev | cut -d" " -f1 | rev)
