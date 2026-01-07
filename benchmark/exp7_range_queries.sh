#!/bin/bash

#####################################################################
# TLF Paper - Experiment 7: Range Queries
# Varies: rs (range size)
# Fixed: n=1000000, u=100, trans=4, z=0, rqp=50
#
# HYTM_MODE (default: true): All impls capped at 2 sockets/72 threads for fair comparison
# Set HYTM_MODE=false to run without hytm and use all available resources
#
# OUTPUT_DIR: If set, write to ${OUTPUT_DIR}/range_queries
#             Otherwise, write to ../../timings/tlf_range_queries_...
#####################################################################

set -e

# Set HYTM_MODE=true for fair comparison (all impls capped at 2 sockets/72 threads)
HYTM_MODE=${HYTM_MODE:-true}

structs="btree arttree"

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
range_sizes="1 10 100 1000 10000 100000 1000000"

# Fixed
default_n="1000000"
default_u="100"
default_trans="4"
default_z="0"
default_p="${max_threads}"
default_rqp="50"
rounds="3"
trial_time="1"

procs=$(nproc)
today=$(date +"%m-%d-%Y")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev | tr -d '[:space:]')

if [ -n "${OUTPUT_DIR}" ]; then
    filename="${OUTPUT_DIR}/range_queries"
else
    filename="../../timings/tlf_range_queries_${procs}-${machine}_${today}${mode_suffix}"
fi

echo "Output: ${filename}"
echo "HYTM_MODE: ${HYTM_MODE} (max_threads=${max_threads})"
> "${filename}"

echo "Date: ${today}" >> "${filename}"
echo "Experiment: Range Queries" >> "${filename}"
echo "HYTM_MODE: ${HYTM_MODE}" >> "${filename}"
echo $(lscpu | grep 'Model name') >> "${filename}"
echo "Fixed: n=${default_n}, u=${default_u}, trans=${default_trans}, z=${default_z}, p=${default_p}, rqp=${default_rqp}" >> "${filename}"
echo "---" >> "${filename}"

run_bench() {
    local cmd="$1"
    ${head} ${cmd}
}

for impl in ${impls}; do
    for struct in ${structs}; do
        if [ ! -f "./${struct}_${impl}" ]; then
            continue
        fi
        echo "Structure: ${struct}, Impl: ${impl}"
        
        for rs in ${range_sizes}; do
            output=$(run_bench "./${struct}_${impl} -nowarmup -n ${default_n} -u ${default_u} -z ${default_z} -r ${rounds} -trans ${default_trans} -rs ${rs} -rqp ${default_rqp} -p ${default_p} -t ${trial_time}" 2>&1) || true
            rqmops=$(echo "$output" | grep -oP 'rqmops=\K[0-9.eE+-]+' | head -1)
            mops=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
            if [ ! -z "$rqmops" ]; then
                echo "${struct},${impl},rs=${rs},rqmops=${rqmops},mops=${mops}" >> "${filename}"
            fi
        done
    done
done

echo "Done! Results: ${filename}"
