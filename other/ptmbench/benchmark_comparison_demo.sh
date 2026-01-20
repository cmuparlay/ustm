#!/bin/bash

echo "=== NV-HALT Benchmark Comparison Demo ==="
echo "Comparing NV-HALT performance with NVRAM enabled vs disabled"
echo

cd /home/zkent/ptmbench/bin

echo "Parameters:"
echo "- 4 worker threads"
echo "- 5% inserts, 5% deletes, 90% lookups"  
echo "- Key range: 1-100000"
echo "- Run time: 3 seconds"
echo

echo "=== 1. NV-HALT with NVRAM enabled ==="
timeout 30 ./abtree.none.phytm1_fgl_colocated_locks -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 100000 -t 3000 | grep -E "(total_throughput|elapsed|TM:|PMEM|HTM)"

echo
echo "=== 2. NV-HALT with NVRAM disabled (no_pmem) ==="
timeout 30 ./abtree.none.phytm1_fgl_colocated_locks_no_pmem -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 100000 -t 3000 | grep -E "(total_throughput|elapsed|TM:|PMEM|HTM)"

echo
echo "=== 3. NV-HALT with flush/fence operations disabled ==="
timeout 30 ./abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 100000 -t 3000 | grep -E "(total_throughput|elapsed|TM:|PMEM|HTM)"

echo
echo "=== Summary ==="
echo "You can see the difference in throughput between:"
echo "1. Full NV-HALT with NVRAM persistence"
echo "2. NV-HALT with NVRAM completely disabled (no_pmem)"
echo "3. NV-HALT with flush/fence operations disabled (noop_flush_fence)"
echo
echo "The 'no_pmem' variant should be closest to a pure STM system"
echo "for comparison against your own STM implementation."