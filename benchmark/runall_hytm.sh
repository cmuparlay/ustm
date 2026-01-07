#!/bin/bash

# tree and hash structures (larger n)
structs="btree arttree leaftree skiplist treap avltree"
sizes="100000 10000000"

# list structures (use smaller n)
lstructs="list"
lsizes="100 1000"

# structures that support range queries
range_structs="btree arttree"

# structures for tpcc
tpcc_structs="btree arttree mix"

# require non-trivial types
xstructs="hash_probe"

# only hytm implementation
impl="hytm"

# Individual test parameters (now separated for individual runs)
update_percents="0 5 50"
zipfian_params="0 .99"
trans_size="4"
rounds="1"

# Range query parameters
range_sizes="1 10 100 1000 10000 100000 1000000"
range_n="1000000"
range_update="100"
range_zipf="0"
range_trans="4"
range_query_percent="50"

# numactl settings for hytm
head="numactl --cpunodebind=0,1 --membind=0,1"

# print various information about the machine/compiler/date at head of file
procs=`nproc`
today=`date +"%m-%d-%Y"`
machine=`lscpu | grep 'Model name' | rev | cut -c 1-5 | rev`
machine_nowhite=${machine//[[:space:]]/}
filename=../../timings/${procs}-${machine}_${today}_hytm_${trans_size}_foo

echo ${filename}
> ${filename}
echo "Date:" ${today} >> ${filename}
echo `lscpu | grep 'Model name'` >> ${filename}
echo "Git branch:" `git rev-parse --short HEAD` >> ${filename}
echo "Compiler:" `g++ --version | head -1` >> ${filename}
echo "OS:" `lsb_release -a | grep 'Description' | cut -c 14-` >> ${filename}
echo "Hugepage:" `cat /sys/kernel/mm/transparent_hugepage/enabled` >> ${filename}
echo "Threads (capped): 72" >> ${filename}

# Helper function to calculate geometric mean
geomean() {
    local product=1
    local count=0
    for val in "$@"; do
        product=$(awk "BEGIN {print $product * $val}")
        count=$((count + 1))
    done
    if [ $count -gt 0 ]; then
        awk "BEGIN {print $product ^ (1.0 / $count)}"
    else
        echo "0"
    fi
}

# # tpcc queries
# for struct in ${tpcc_structs}
# do
#     echo tpcc ${struct}
#     ${head} ./tpcc_${struct}_${impl} -nowarmup -r 3 >> ${filename}
# done

# # non-trivial types
# for struct in ${xstructs}
# do
#     echo ${struct}
#     ${head} ./${struct}_${impl} ${sizes} ${geomean_args} >> ${filename}
# done

# non list structures - run each configuration individually and compute geomean
for struct in ${structs}
do
    echo "${struct}" >> ${filename}
    throughputs=()
    for n in ${sizes}
    do
        for u in ${update_percents}
        do
            for z in ${zipfian_params}
            do
                output=$(${head} ./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${rounds} -trans ${trans_size} -p 72)
                result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                result_line=$(echo "$output" | grep 'mops=')
                if [ ! -z "$result_line" ]; then
                    echo "$result_line" >> ${filename}
                fi
                if [ ! -z "$result" ]; then
                    throughputs+=($result)
                fi
            done
        done
    done
    if [ ${#throughputs[@]} -gt 0 ]; then
        gmean=$(geomean "${throughputs[@]}")
        echo "geomean throughput: ${gmean}" >> ${filename}
    fi
done

# list structures - run each configuration individually and compute geomean
for struct in ${lstructs}
do
    echo "${struct}" >> ${filename}
    throughputs=()
    for n in ${lsizes}
    do
        for u in ${update_percents}
        do
            for z in ${zipfian_params}
            do
                output=$(${head} ./${struct}_${impl} -nowarmup -n ${n} -u ${u} -z ${z} -r ${rounds} -trans ${trans_size} -p 72)
                result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
                result_line=$(echo "$output" | grep 'mops=')
                if [ ! -z "$result_line" ]; then
                    echo "$result_line" >> ${filename}
                fi
                if [ ! -z "$result" ]; then
                    throughputs+=($result)
                fi
            done
        done
    done
    if [ ${#throughputs[@]} -gt 0 ]; then
        gmean=$(geomean "${throughputs[@]}")
        echo "geomean throughput: ${gmean}" >> ${filename}
    fi
done

# range queries - run each range size individually
for struct in ${range_structs}
do
    echo "${struct} range queries" >> ${filename}
    for rs in ${range_sizes}
    do
        output=$(${head} ./${struct}_${impl} -n ${range_n} -u ${range_update} -z ${range_zipf} -trans ${range_trans} -rs ${rs} -rqp ${range_query_percent} -p 72)
        result=$(echo "$output" | grep -oP 'mops=\K[0-9.]+' | head -1)
        result_line=$(echo "$output" | grep 'mops=')
        if [ ! -z "$result_line" ]; then
            echo "$result_line" >> ${filename}
        fi
        if [ ! -z "$result" ]; then
            echo "range_size: ${rs}, throughput: ${result}" >> ${filename}
        fi
    done
done
