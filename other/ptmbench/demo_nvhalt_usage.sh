#!/bin/bash

echo "=== NV-HALT Usage Demonstration ==="
echo "This script shows how to run NV-HALT with NVRAM disabled"
echo

# Example 1: Running NV-HALT with NVRAM disabled (no persistent memory)
echo "1. Running NV-HALT with NVRAM completely disabled:"
echo "   ./bin/abtree.none.phytm1_fgl_colocated_locks_no_pmem -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 1000000 -t 5000"
echo

# Example 2: Running NV-HALT with flush/fence operations disabled (but pmem structures still used)
echo "2. Running NV-HALT with flush/fence operations disabled:"
echo "   ./bin/abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 1000000 -t 5000"
echo

# Example 3: Running regular NV-HALT for comparison
echo "3. Running regular NV-HALT (with NVRAM enabled):"
echo "   ./bin/abtree.none.phytm1_fgl_colocated_locks -nwork 4 -nprefill 4 -insdel 5.0 5.0 -k 1000000 -t 5000"
echo

echo "=== Running actual demonstration ==="
echo

# Change to bin directory
cd /home/zkent/ptmbench/bin

echo "Testing NV-HALT with NVRAM disabled (no_pmem variant):"
timeout 30 ./abtree.none.phytm1_fgl_colocated_locks_no_pmem -nwork 2 -nprefill 2 -insdel 5.0 5.0 -k 100000 -t 2000
echo

echo "Testing NV-HALT with flush/fence disabled (noop_flush_fence variant):"
timeout 30 ./abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence -nwork 2 -nprefill 2 -insdel 5.0 5.0 -k 100000 -t 2000
echo

echo "=== Command line parameters explanation ==="
echo "-nwork 2        : Use 2 worker threads"
echo "-nprefill 2     : Use 2 threads for prefilling the data structure"
echo "-insdel 5.0 5.0 : 5% inserts, 5% deletes, 90% lookups"
echo "-k 100000       : Key range from 0 to 100000"
echo "-t 2000         : Run for 2000 milliseconds"
echo

echo "=== Available NV-HALT variants ==="
echo "abtree.none.phytm1_fgl_colocated_locks                    - Regular NV-HALT with NVRAM"
echo "abtree.none.phytm1_fgl_colocated_locks_no_pmem           - NV-HALT with NVRAM completely disabled"
echo "abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence  - NV-HALT with flush/fence disabled"
echo "abtree.none.phytm1_fgl_colocated_locks_no_phtm          - NV-HALT fully volatile (no persistent HTM)"