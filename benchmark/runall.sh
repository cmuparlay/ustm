structs="hash_block btree arttree leaftree skiplist treap"
lstructs="list"
rstructs="btree arttree"
impls="ustm mvustm mvustm_hwstamp mvustm_longjmp mvustm_trythrow mvustm_unprotected 2plsf"
sizes="-n 100000,10000000"
lsizes="-n 100,1000"

geomean_args="-u 0,5,50 -z 0,.99 -r 1 -trans 16"
range_args="-n 1000000 -u 100 -z 0 -trans 16 -rs 1,10,100,1000,10000,100000,1000000 -rqp 50"

head="numactl -i all"
procs=`nproc`
today=`date +"%m-%d-%Y"`
machine=`lscpu | grep 'Model name' | rev | cut -c 1-5 | rev`
filename=../../timings/${procs}-${machine}_${today}

echo ${filename}
> ${filename}
echo ${today} >> ${filename}
echo `lscpu | grep 'Model name'` >> ${filename}
echo "git branch: " `git rev-parse --short HEAD` >> ${filename}
echo `g++ --version | head -1` >> ${filename}
for struct in ${structs}
do
    echo ${struct}
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${sizes} ${geomean_args} >> ${filename}
    done
done
echo "Done 1"
for struct in ${lstructs}
do
    echo ${struct}
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${lsizes} ${geomean_args} >> ${filename}
    done
done

for struct in ${rstructs}
do
    for impl in ${impls}
    do
        ${head} ./${struct}_${impl} ${range_args} >> ${filename}
    done
done
