#!/usr/bin/env bash
# Push-button driver for the txn_overhead family of experiments.
#
# Builds every binary the sweeps need, runs vary_trans_focus
# (1x3 paper-row plot: btree / avltree / hash_block, vary trans)
# and txn_overhead (full geomean sweep across 8 OL structures at
# trans=4), then regenerates the plots into plot/plots/.
#
# Usage:
#   scripts/run_txn_overhead.sh             # all three: build + sweep + plot
#   scripts/run_txn_overhead.sh build       # build only
#   scripts/run_txn_overhead.sh sweep       # sweep only (assumes binaries exist)
#   scripts/run_txn_overhead.sh plot        # plot only (assumes timings exist)
#
# Wall-clock: ~10-15 min build + ~10 min vary_trans_focus
# + ~60-80 min txn_overhead full geomean on a 144-core machine.

set -euo pipefail

# Locate the project root (parent of this script's directory).
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TIMINGS=$ROOT/timings
PLOTS=$ROOT/plot/plots
DATE=$(date +%m-%d-%Y)
MACHINE_TAG="144-40GHz"   # matches setup_output() naming in run_experiment.py
JOBS=$(nproc)

step_build() {
  echo "== Configuring and building (j=$JOBS) =="
  cmake -S . -B build
  cmake --build build -- -j "$JOBS"
}

step_sweep() {
  echo "== Running vary_trans_focus =="
  (cd benchmark && python3 run_experiment.py vary_trans_focus)
  echo "== Running txn_overhead =="
  (cd benchmark && python3 run_experiment.py txn_overhead)
}

step_plot() {
  echo "== Staging timings and rendering plots =="
  STAGE=$(mktemp -d)
  trap "rm -rf $STAGE" EXIT
  for exp in vary_trans_focus txn_overhead; do
    src=$TIMINGS/${exp}_${MACHINE_TAG}_${DATE}
    if [[ ! -f $src ]]; then
      # Fall back to the newest file for this experiment if today's
      # didn't get produced (e.g. plot-only invocation).
      src=$(ls -t "$TIMINGS"/${exp}_${MACHINE_TAG}_* 2>/dev/null | head -1 || true)
    fi
    if [[ -f $src ]]; then
      ln -sf "$src" "$STAGE/$exp"
      echo "  $exp -> $src"
    else
      echo "  WARNING: no timings file for $exp" >&2
    fi
  done
  python3 plot/plot.py "$STAGE" --plots txn_overhead -o "$STAGE/plots"
  mkdir -p "$PLOTS"
  cp "$STAGE/plots/"trans_focus.pdf "$STAGE/plots/"txn_overhead.pdf "$PLOTS/" 2>/dev/null || true
  echo "== Done. Plots in $PLOTS/ =="
}

case "${1:-all}" in
  build) step_build ;;
  sweep) step_sweep ;;
  plot)  step_plot ;;
  all)   step_build; step_sweep; step_plot ;;
  *)     echo "usage: $0 [build|sweep|plot|all]" >&2; exit 1 ;;
esac
