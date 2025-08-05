# bash

# tree and hash structures (larger n)
structs="hash_block btree arttree leaftree skiplist treap avltree"
sizes="-n 100000,10000000"

# list structures (use smaller n)
lstructs="list"
lsizes="-n 100,1000"

# structures that support range queries
rstructs="btree arttree"

# require non-trivial types
xstructs="hash_probe"

# implementations that support trivial types (8-byte, trivially copyable)
impls="mvustm mvustm_hwstamp ustm_longjmp mvustm_longjmp mvustm_trythrow mvustm_unprotected 2plsf"

# implementations that support general relocatable types (subset of above)
general_impls="mvustm mvustm_hwstamp mvustm_longjmp mvustm_trythrow mvustm_unprotected"

# arguments that scan over update percent (-u), and zipfian (-z), using
# transactions of size 16, reporting the geometric mean of throughput
geomean_args="-u 0,5,50 -z 0,.99 -r 1 -trans 16"

# arguments that scan over different range sizes for half the threads
# doing range queries (-rqp 50) and the rest just doing updates
range_args="-n 1000000 -u 100 -z 0 -trans 16 -rs 1,10,100,1000,10000,100000,1000000 -rqp 50"

# common header (useful on multinode numa machines)
head="numactl -i all"

# print various imformation about the machine/compiler/date at head of file
procs=`nproc`
today=`date +"%m-%d-%Y"`
machine=`lscpu | grep 'Model name' | rev | cut -c 1-5 | rev`
machine_nowhite=${machine//[[:space:]]/}
filename=../../timings/${procs}-${machine}_${today}

echo ${filename}
> ${filename}
echo "Date:" ${today} >> ${filename}
echo `lscpu | grep 'Model name'` >> ${filename}
echo "Git branch:" `git rev-parse --short HEAD` >> ${filename}
echo "Compiler:" `g++ --version | head -1` >> ${filename}
echo "Hugepage:" `cat /sys/kernel/mm/transparent_hugepage/enabled` >> ${filename}

# non-trivial types
for struct in ${xstructs}
do
    echo ${struct}
    for impl in ${general_impls}
    do
        ${head} ./${struct}_${impl} ${sizes} ${geomean_args} >> ${filename}
    done
done

# non list structures
for struct in ${structs}
do
    echo ${struct}
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${sizes} ${geomean_args} >> ${filename}
    done
done

# list structures
for struct in ${lstructs}
do
    echo ${struct}
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${lsizes} ${geomean_args} >> ${filename}
    done
done

# range queries
for struct in ${rstructs}
do
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${range_args} >> ${filename}
    done
done
