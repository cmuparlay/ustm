#!/bin/bash

#jax
pinning="0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191"
numactlMode="numactl --interleave=all"

#nasus
# pinning="64-127,192-255,0-63,128-191" 
# numactlMode="numactl -i 1"

# zyra
# pinning="0-17,72-89,18-35,90-107,36-53,108-125,54-71,126-143"
# numactlMode="numactl --interleave=all"

program="./bin/db_multiverse"
# program="./bin/db_dctl"

prefillThreads=128
workThreads=8
companyAnalyticsThreads=0

# txnDistribution="-txn-add-order 100"
txnDistribution="-txn-add-order 99 -txn-company-analytics 1"
kcategories="10000"
kcustomers="100000000"
kitems="1000000000"
korders="1000000000"
kdates="10000"
max_items_per_order="15"

prefillitems="10000000" 
prefillorders="10000000"

rqsize="8"

LD_PRELOAD=../setbench/lib/libmimalloc.so timeout 360 $numactlMode time $program -t 20000 -rqsize $rqsize -nprefill $prefillThreads -n $workThreads -nca $companyAnalyticsThreads -cats 0 -zipf 0.9  -kcategories $kcategories -kcustomers $kcustomers -kitems $kitems -korders $korders -kdates $kdates -max-items-per-order $max_items_per_order -prefillitems $prefillitems -prefillorders $prefillorders $txnDistribution -pin $pinning