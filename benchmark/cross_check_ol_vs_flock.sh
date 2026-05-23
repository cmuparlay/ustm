#!/usr/bin/env bash
# Cross-validate ${bench}_ol against ${bench}_flock by running both with
# the same parameter sweep and comparing per-struct geomeans of throughput
# (mops). Both binaries link the same plain blocking OL data structures
# from other/fuse/include/structures (compiled with just "Ol"); the only
# difference is the surrounding benchmark loop:
#   _ol    -- benchmark/mixed_ol.cpp (this repo, single-op YCSB)
#   _flock -- other/fuse/benchmark/test_sets.cpp (upstream cmuparlay/flock)
#
# Usage:
#   cd path/to/build/benchmark && bash <repo>/benchmark/cross_check_ol_vs_flock.sh
# Outputs (in cwd):
#   cross_check.log -- per-struct geomeans + ratio
#   cross_check.raw -- one row per (struct, n, u, z) config
set -u

OUT=${OUT:-cross_check.log}
RAW=${RAW:-cross_check.raw}
: > "$OUT"
: > "$RAW"

structs=(btree arttree avltree leaftree treap skiplist hash_block)
list_structs=(list)

P=${P:-$(nproc)}
T=${T:-3}             # seconds per trial
ROUNDS=${ROUNDS:-2}   # trials per config
PRELOAD_PREFIX="LD_PRELOAD=${LD_PRELOAD:-../libdeqalloc.so} numactl -i all"

run_ol() {
  local bin=$1 n=$2 u=$3 z=$4
  eval $PRELOAD_PREFIX ./${bin} -n $n -u $u -z $z -p $P -t $T -r $ROUNDS -nowarmup 2>&1 \
    | grep -E "mops=" | sed 's/.*mops=//'
}

run_flock() {
  local bin=$1 n=$2 u=$3 z=$4
  eval $PRELOAD_PREFIX ./${bin} -n $n -u $u -z $z -p $P -t $T -r $ROUNDS -nowarmup 2>&1 \
    | grep -E "^./" | awk -F, '{print $NF}'
}

gm() {
  python3 -c "
import sys, math
vals = [float(x) for x in '''$1'''.split() if x]
if not vals: print('-'); sys.exit()
print(f'{math.exp(sum(math.log(v) for v in vals)/len(vals)):.3f}')"
}

# n × u × z sweep, same as run_stm_vs_ol
sizes_main=(100000 1000000 10000000)
sizes_list=(100 1000)
updates=(0 5 50)
zipfians=(0 0.75 0.99)

sweep_struct() {
  local s=$1
  local -n sizes_ref=$2
  local ol_vals="" fl_vals=""
  for n in "${sizes_ref[@]}"; do
    for u in "${updates[@]}"; do
      for z in "${zipfians[@]}"; do
        local v_ol v_fl
        v_ol=$(run_ol "${s}_ol" "$n" "$u" "$z")
        v_fl=$(run_flock "${s}_flock" "$n" "$u" "$z")
        echo "$s,n=$n,u=$u,z=$z,ol=$v_ol,flock=$v_fl" >> "$RAW"
        ol_vals="$ol_vals $v_ol"
        fl_vals="$fl_vals $v_fl"
      done
    done
  done
  local g_ol g_fl
  g_ol=$(gm "$ol_vals")
  g_fl=$(gm "$fl_vals")
  local ratio
  ratio=$(python3 -c "print(f'{float(\"$g_ol\")/float(\"$g_fl\"):.3f}')" 2>/dev/null || echo "-")
  printf "%-12s  ol_GM=%-8s  flock_GM=%-8s  ol/flock=%s\n" "$s" "$g_ol" "$g_fl" "$ratio" | tee -a "$OUT"
}

echo "Cross-check sweep: n × u × z, p=$P, t=$T, rounds=$ROUNDS, trans=1" | tee -a "$OUT"
echo "===" | tee -a "$OUT"

for s in "${structs[@]}"; do
  sweep_struct "$s" sizes_main
done
for s in "${list_structs[@]}"; do
  sweep_struct "$s" sizes_list
done

echo "===" | tee -a "$OUT"
echo "Done. Per-config rows in $RAW; summary in $OUT" | tee -a "$OUT"
