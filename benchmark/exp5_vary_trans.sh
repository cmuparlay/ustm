#!/bin/bash

#####################################################################
# TLF Paper - Experiment 5: Varying Transaction Size (Figures 9/10, Row 4)
# Varies: trans (operations per transaction)
# Fixed: n=100000 (300 for list), u=5, z=0.9
#
# HYTM_MODE (default: true): All impls capped at 2 sockets/72 threads for fair comparison
# Set HYTM_MODE=false to run without hytm and use all available resources
#####################################################################

set -e

# Set HYTM_MODE=true for fair comparison (all impls capped at 2 sockets/72 threads)
HYTM_MODE=${HYTM_MODE:-true}

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

# Varying parameter
trans_sizes="1 2 4 8 16 32"

# Fixed defaults
default_n="100000"
default_list_n="300"
default_u="5"
default_z="0.9"
default_p="${max_threads}"
rounds="3"
trial_time="1"

procs=$(nproc)
today=$(date +"%m-%d-%Y")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev | tr -d '[:space:]')
filename="../../timings/tlf_vary_trans_${procs}-${machine}_${today}${mode_suffix}"

echo "Output: ${filename}"
echo "HYTM_MODE: ${HYTM_MODE} (max_threads=${max_threads})"
> "${filename}"

echo "Date: ${today}" >> "${filename}"
echo "Experiment: Varying Transaction Size (Figures 9/10 Row 4)" >> "${filename}"
echo "HYTM_MODE: ${HYTM_MODE}" >> "${filename}"
echo $(lscpu | grep 'Model name') >> "${filename}"
echo "Fixed: n=${default_n} (${default_list_n} for list), u=${default_u}, z=${default_z}, p=${default_p}" >> "${filename}"
echo "---" >> "${filename}"

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
        
        for trans in ${trans_sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${default_u} -z ${default_z} -r ${rounds} -trans ${trans} -p ${default_p} -t ${trial_time}" 2>&1) || true
            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
            if [ ! -z "$result" ]; then
                echo "${struct},${impl},trans=${trans},mops=${result}" >> "${filename}"
            fi
        done
    done
done

# List structures
for impl in ${impls}; do
    for struct in ${lstructs}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "Structure: ${struct}, Impl: ${impl}"
        
        for trans in ${trans_sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_list_n} -u ${default_u} -z ${default_z} -r ${rounds} -trans ${trans} -p ${default_p} -t ${trial_time}" 2>&1) || true
            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
            if [ ! -z "$result" ]; then
                echo "${struct},${impl},trans=${trans},mops=${result}" >> "${filename}"
            fi
        done
    done
done

echo "Done! Results: ${filename}"
