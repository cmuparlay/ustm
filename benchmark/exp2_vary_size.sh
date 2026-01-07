#!/bin/bash

#####################################################################
# TLF Paper - Experiment 2: Varying Size (Figures 9/10, Row 1)
# Varies: n
# Fixed: u=5, trans=4, z=0.9
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
sizes="1000 10000 100000 1000000 10000000 100000000"
list_sizes="10 100 300 1000 3000 10000"

# Fixed defaults (from paper Section 10)
default_u="5"
default_trans="4"
default_z="0.9"
default_p="${max_threads}"
rounds="3"
trial_time="1"

procs=$(nproc)
today=$(date +"%m-%d-%Y")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev | tr -d '[:space:]')
filename="../../timings/tlf_vary_size_${procs}-${machine}_${today}${mode_suffix}"

echo "Output: ${filename}"
echo "HYTM_MODE: ${HYTM_MODE} (max_threads=${max_threads})"
> "${filename}"

echo "Date: ${today}" >> "${filename}"
echo "Experiment: Varying Size (Figures 9/10 Row 1)" >> "${filename}"
echo "HYTM_MODE: ${HYTM_MODE}" >> "${filename}"
echo $(lscpu | grep 'Model name') >> "${filename}"
echo "Fixed: u=${default_u}, trans=${default_trans}, z=${default_z}, p=${default_p}" >> "${filename}"
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
        
        for n in ${sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${default_u} -z ${default_z} -r ${rounds} -trans ${default_trans} -p ${default_p} -t ${trial_time}" 2>&1) || true
            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
            if [ ! -z "$result" ]; then
                echo "${struct},${impl},n=${n},mops=${result}" >> "${filename}"
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
        
        for n in ${list_sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${n} -u ${default_u} -z ${default_z} -r ${rounds} -trans ${default_trans} -p ${default_p} -t ${trial_time}" 2>&1) || true
            result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
            if [ ! -z "$result" ]; then
                echo "${struct},${impl},n=${n},mops=${result}" >> "${filename}"
            fi
        done
    done
done

echo "Done! Results: ${filename}"
