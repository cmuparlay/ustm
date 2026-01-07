#!/bin/bash

#####################################################################
# TLF Paper - Experiment 1: Geometric Mean Performance (Figure 1)
# Varies: n, update%, trans_size, zipfian (all combinations)
# Purpose: Compute geometric mean throughput per structure/impl
#
# HYTM_MODE (default: true): All impls capped at 2 sockets/72 threads for fair comparison
# Set HYTM_MODE=false to run without hytm and use all available resources
#
# OUTPUT_DIR: If set, write to ${OUTPUT_DIR}/geomean
#             Otherwise, write to ../../timings/tlf_geomean_...
#####################################################################

set -e

# Set HYTM_MODE=true for fair comparison (all impls capped at 2 sockets/72 threads)
HYTM_MODE=${HYTM_MODE:-true}

# Data structures
structs="btree arttree avltree leaftree treap skiplist hash_block"
structs_nohash="btree arttree avltree leaftree treap skiplist"  # hytm incompatible with hash
lstructs="list"

if [ "$HYTM_MODE" = "true" ]; then
    impls="mvustm mvustm_hwstamp mvustm_longjmp 2plsf hytm"
    max_threads="72"
    head="numactl --cpunodebind=0,1 --membind=0,1"
    mode_suffix="_hytm"
else
    impls="mvustm mvustm_hwstamp mvustm_longjmp 2plsf"
    max_threads="191"
    head="numactl -i all"
    mode_suffix=""
fi

# Parameters from paper Section 10.1
geomean_sizes="10000 1000000 100000000"  # n ∈ {10^4, 10^6, 10^8}
list_sizes="100 1000"                     # n ∈ {10^2, 10^3} for list
update_percents="0 5 50"
trans_sizes="1 4 16"
zipfian_params="0 0.99"

# Fixed
threads="${max_threads}"
rounds="3"
trial_time="1"

# Output file - use OUTPUT_DIR if set, otherwise legacy path
procs=$(nproc)
today=$(date +"%m-%d-%Y")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev | tr -d '[:space:]')

if [ -n "${OUTPUT_DIR}" ]; then
    filename="${OUTPUT_DIR}/geomean"
else
    filename="../../timings/tlf_geomean_${procs}-${machine}_${today}${mode_suffix}"
fi

echo "Output: ${filename}"
echo "HYTM_MODE: ${HYTM_MODE} (max_threads=${max_threads})"
> "${filename}"

# Header
echo "Date: ${today}" >> "${filename}"
echo "Experiment: Geometric Mean (Figure 1)" >> "${filename}"
echo "HYTM_MODE: ${HYTM_MODE}" >> "${filename}"
echo $(lscpu | grep 'Model name') >> "${filename}"
echo "Git branch: $(git rev-parse --short HEAD 2>/dev/null || echo 'unknown')" >> "${filename}"
echo "Threads: ${threads}" >> "${filename}"
echo "---" >> "${filename}"

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

run_bench() {
    local cmd="$1"
    ${head} ${cmd}
}

# Get structures for this impl (hytm incompatible with hash)
get_structs() {
    local impl="$1"
    if [ "$impl" = "hytm" ]; then
        echo "${structs_nohash}"
    else
        echo "${structs}"
    fi
}

# Tree/hash structures
for impl in ${impls}; do
    impl_structs=$(get_structs "$impl")
    for struct in ${impl_structs}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "Structure: ${struct}, Impl: ${impl}"
        
        throughputs=()
        for n in ${geomean_sizes}; do
            for u in ${update_percents}; do
                for trans in ${trans_sizes}; do
                    for z in ${zipfian_params}; do
                        output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${rounds} -trans ${trans} -p ${threads} -t ${trial_time}" 2>&1) || true
                        result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                        if [ ! -z "$result" ]; then
                            throughputs+=($result)
                        fi
                    done
                done
            done
        done
        
        if [ ${#throughputs[@]} -gt 0 ]; then
            gmean=$(geomean "${throughputs[@]}")
            echo "${struct},${impl},geomean=${gmean}" >> "${filename}"
        fi
    done
done

# List structures (smaller n)
for impl in ${impls}; do
    for struct in ${lstructs}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "Structure: ${struct}, Impl: ${impl}"
        
        throughputs=()
        for n in ${list_sizes}; do
            for u in ${update_percents}; do
                for trans in ${trans_sizes}; do
                    for z in ${zipfian_params}; do
                        output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${rounds} -trans ${trans} -p ${threads} -t ${trial_time}" 2>&1) || true
                        result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                        if [ ! -z "$result" ]; then
                            throughputs+=($result)
                        fi
                    done
                done
            done
        done
        
        if [ ${#throughputs[@]} -gt 0 ]; then
            gmean=$(geomean "${throughputs[@]}")
            echo "${struct},${impl},geomean=${gmean}" >> "${filename}"
        fi
    done
done

echo "Done! Results: ${filename}"
