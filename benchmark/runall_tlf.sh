#!/bin/bash

#####################################################################
# TLF Paper Experiments Runner
# Recreates experiments from:
# "TLF: Transactional Lock Fusion" (SPAA '25)
# https://doi.org/10.1145/3694906.3743341
#
# This script runs the complete set of experiments varying:
# - Data structure size (n)
# - Update percentage (u)
# - Transaction size (trans)
# - Zipfian parameter (z)
# - Number of threads (p)
#####################################################################

set -e

# Data structures (same as paper)
# Tree/hash structures (larger n)
structs="btree arttree avltree leaftree treap skiplist hash_block"

# List structures (smaller n for performance)
lstructs="list"

# Structures that support range queries
range_structs="btree arttree"

# All implementations to test
# Paper tests: 2PLSF, 2PLSF-TLF-STM, MV-STM, MV-TLF
# Our implementations: mvustm (MV-STM), mvustm_hwstamp, ustm_longjmp, 2plsf, hytm
impls="mvustm mvustm_hwstamp mvustm_longjmp 2plsf hytm"

# Experiment parameters from paper
# Section 10: Geometric Mean Performance experiments
geomean_sizes="10000 1000000 100000000"  # n ∈ {10^4, 10^6, 10^8}
list_sizes="100 1000 10000"               # smaller sizes for list
update_percents="0 5 50"                  # Update % ∈ {0%, 5%, 50%}
trans_sizes="1 4 16"                      # Transaction size ∈ {1, 4, 16}
zipfian_params="0 0.99"                   # Zipfian ∈ {0, 0.99}

# Comma-separated args for single-invocation geometric mean (more efficient)
# Used for non-HyTM implementations which don't have memory leaks
geomean_args="-n 10000,1000000,100000000 -u 0,5,50 -z 0,0.99 -trans 1,4,16"
list_geomean_args="-n 100,1000,10000 -u 0,5,50 -z 0,0.99 -trans 1,4,16"

# Section 10.2: Varying Parameters experiments
scalability_threads="1 4 8 16 32 48 64 96 128 192"
varying_sizes="1000 10000 100000 1000000 10000000 100000000"
varying_zipfian="0 0.25 0.5 0.75 0.9 0.95 0.99"
varying_trans="1 2 4 8 16 32"
varying_update="0 1 5 10 20 50"

# Range query parameters
range_sizes="1 10 100 1000 10000 100000 1000000"
range_n="1000000"
range_update="100"
range_zipf="0"
range_trans="4"
range_query_percent="50"

# Default parameters (Section 10)
default_n="100000"
default_list_n="300"
default_update="5"
default_trans="4"
default_zipfian="0.9"
default_threads="191"
default_rounds="3"
trial_time="1"

# numactl settings
head="numactl -i all"
hytm_head="numactl --cpunodebind=0,1 --membind=0,1"

# Output file setup
procs=$(nproc)
today=$(date +"%m-%d-%Y")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev)
machine_nowhite=${machine//[[:space:]]/}
timestamp=$(date +"%H%M%S")
filename="../../timings/tlf_${procs}-${machine_nowhite}_${today}_${timestamp}"

echo "Output file: ${filename}"
> "${filename}"

# Print system info
echo "Date: ${today}" >> "${filename}"
echo $(lscpu | grep 'Model name') >> "${filename}"
echo "Git branch: $(git rev-parse --short HEAD)" >> "${filename}"
echo "Compiler: $(g++ --version | head -1)" >> "${filename}"
echo "OS: $(lsb_release -a 2>/dev/null | grep 'Description' | cut -c 14-)" >> "${filename}"
echo "Hugepage: $(cat /sys/kernel/mm/transparent_hugepage/enabled)" >> "${filename}"
echo "Total threads: ${procs}" >> "${filename}"
echo "---" >> "${filename}"

# Helper function to calculate geometric mean
geomean() {
    local product=1
    local count=0
    for val in "$@"; do
        if [ ! -z "$val" ] && [ "$val" != "0" ]; then
            product=$(awk "BEGIN {print $product * $val}")
            count=$((count + 1))
        fi
    done
    if [ $count -gt 0 ]; then
        awk "BEGIN {print $product ^ (1.0 / $count)}"
    else
        echo "0"
    fi
}

# Helper to run benchmark with appropriate numactl
run_bench() {
    local cmd="$1"
    local impl="$2"
    if [ "$impl" = "hytm" ]; then
        ${hytm_head} ${cmd}
    else
        ${head} ${cmd}
    fi
}

#####################################################################
# Experiment 1: Geometric Mean Performance (Figure 1, Section 10.1)
# Vary: n, update%, trans_size, zipfian
# Fixed: threads = default_threads
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 1: Geometric Mean Performance"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 1: Geometric Mean Performance ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "" >> "${filename}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            echo "  Skipping ${impl} (binary not found)"
            continue
        fi
        echo "  Implementation: ${impl}"
        
        if [ "$impl" = "hytm" ]; then
            # HyTM has a memory leak, so we must run each parameter combination separately
            throughputs=()
            for n in ${geomean_sizes}; do
                for u in ${update_percents}; do
                    for trans in ${trans_sizes}; do
                        for z in ${zipfian_params}; do
                            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${default_rounds} -trans ${trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
                            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                            result_line=$(echo "$output" | grep 'mops=')
                            if [ ! -z "$result_line" ]; then
                                echo "./${struct}_${impl},${result_line}" >> "${filename}"
                            fi
                            if [ ! -z "$result" ]; then
                                throughputs+=($result)
                            fi
                        done
                    done
                done
            done
            
            if [ ${#throughputs[@]} -gt 0 ]; then
                gmean=$(geomean "${throughputs[@]}")
                echo "./${struct}_${impl},GEOMETRIC MEAN OF THROUGHPUTS: ${gmean}" >> "${filename}"
            fi
        else
            # Non-HyTM: use single invocation with comma-separated params (more efficient)
            output=$(run_bench "./${struct}_${impl} ${geomean_args} -r ${default_rounds} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            echo "$output" | while read -r line; do
                if echo "$line" | grep -q 'mops='; then
                    echo "./${struct}_${impl},${line}" >> "${filename}"
                elif echo "$line" | grep -q 'GEOMETRIC MEAN'; then
                    echo "./${struct}_${impl},${line}" >> "${filename}"
                fi
            done
        fi
    done
done

# List structures with smaller n
for struct in ${lstructs}; do
    echo "Structure: ${struct}"
    echo "" >> "${filename}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            echo "  Skipping ${impl} (binary not found)"
            continue
        fi
        echo "  Implementation: ${impl}"
        
        if [ "$impl" = "hytm" ]; then
            # HyTM has a memory leak, so we must run each parameter combination separately
            throughputs=()
            for n in ${list_sizes}; do
                for u in ${update_percents}; do
                    for trans in ${trans_sizes}; do
                        for z in ${zipfian_params}; do
                            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${default_rounds} -trans ${trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
                            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                            result_line=$(echo "$output" | grep 'mops=')
                            if [ ! -z "$result_line" ]; then
                                echo "./${struct}_${impl},${result_line}" >> "${filename}"
                            fi
                            if [ ! -z "$result" ]; then
                                throughputs+=($result)
                            fi
                        done
                    done
                done
            done
            
            if [ ${#throughputs[@]} -gt 0 ]; then
                gmean=$(geomean "${throughputs[@]}")
                echo "./${struct}_${impl},GEOMETRIC MEAN OF THROUGHPUTS: ${gmean}" >> "${filename}"
            fi
        else
            # Non-HyTM: use single invocation with comma-separated params (more efficient)
            output=$(run_bench "./${struct}_${impl} ${list_geomean_args} -r ${default_rounds} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            echo "$output" | while read -r line; do
                if echo "$line" | grep -q 'mops='; then
                    echo "./${struct}_${impl},${line}" >> "${filename}"
                elif echo "$line" | grep -q 'GEOMETRIC MEAN'; then
                    echo "./${struct}_${impl},${line}" >> "${filename}"
                fi
            done
        fi
    done
done

#####################################################################
# Experiment 2: Varying Size (Figure 9/10, Row 1)
# Vary: n
# Fixed: update=default_update, trans=default_trans, zipfian=default_zipfian, threads=default_threads
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 2: Varying Size"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 2: Varying Size ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for n in ${varying_sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${default_update} -z ${default_zipfian} -r ${default_rounds} -trans ${default_trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
        done
    done
done

#####################################################################
# Experiment 3: Varying Thread Count (Figure 9/10, Row 2)
# Vary: threads
# Fixed: n=default_n, update=default_update, trans=default_trans, zipfian=default_zipfian
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 3: Varying Thread Count"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 3: Varying Thread Count ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for p in ${scalability_threads}; do
            # Cap threads at available processors
            if [ $p -gt $procs ]; then
                continue
            fi
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${default_update} -z ${default_zipfian} -r ${default_rounds} -trans ${default_trans} -p ${p} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
        done
    done
done

#####################################################################
# Experiment 4: Varying Zipfian/Skew (Figure 9/10, Row 3)
# Vary: zipfian
# Fixed: n=default_n, update=default_update, trans=default_trans, threads=default_threads
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 4: Varying Zipfian"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 4: Varying Zipfian ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for z in ${varying_zipfian}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${default_update} -z ${z} -r ${default_rounds} -trans ${default_trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
        done
    done
done

#####################################################################
# Experiment 5: Varying Transaction Size (Figure 9/10, Row 4)
# Vary: trans
# Fixed: n=default_n, update=default_update, zipfian=default_zipfian, threads=default_threads
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 5: Varying Transaction Size"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 5: Varying Transaction Size ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for trans in ${varying_trans}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${default_update} -z ${default_zipfian} -r ${default_rounds} -trans ${trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
        done
    done
done

#####################################################################
# Experiment 6: Varying Update Rate (Figure 9/10, Row 5)
# Vary: update
# Fixed: n=default_n, trans=default_trans, zipfian=default_zipfian, threads=default_threads
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 6: Varying Update Rate"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 6: Varying Update Rate ===" >> "${filename}"

for struct in ${structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct}" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for u in ${varying_update}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${u} -z ${default_zipfian} -r ${default_rounds} -trans ${default_trans} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
        done
    done
done

#####################################################################
# Experiment 7: Range Queries (Section 10, Range benchmarks)
# Vary: range_size
# Fixed: n=range_n, update=range_update, trans=range_trans, zipfian=range_zipf, rqp=range_query_percent
#####################################################################
echo ""
echo "=========================================="
echo "EXPERIMENT 7: Range Queries"
echo "=========================================="
echo "" >> "${filename}"
echo "=== EXPERIMENT 7: Range Queries ===" >> "${filename}"

for struct in ${range_structs}; do
    echo "Structure: ${struct}"
    echo "STRUCT: ${struct} range queries" >> "${filename}"
    
    for impl in ${impls}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "  Implementation: ${impl}"
        
        for rs in ${range_sizes}; do
            output=$(run_bench "./${struct}_${impl} -n ${range_n} -u ${range_update} -z ${range_zipf} -trans ${range_trans} -rs ${rs} -rqp ${range_query_percent} -p ${default_threads} -t ${trial_time}" "${impl}" 2>&1) || true
            result_line=$(echo "$output" | grep 'mops=')
            rqmops=$(echo "$output" | grep -oP 'rqmops=\K[0-9.e+-]+' | head -1)
            if [ ! -z "$result_line" ]; then
                echo "./${struct}_${impl},${result_line}" >> "${filename}"
            fi
            if [ ! -z "$rqmops" ]; then
                echo "range_size: ${rs}, throughput: ${rqmops}" >> "${filename}"
            fi
        done
    done
done

echo ""
echo "=========================================="
echo "All experiments complete!"
echo "Results saved to: ${filename}"
echo "=========================================="
