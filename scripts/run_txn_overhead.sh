#!/usr/bin/env bash
# Push-button driver for the txn_overhead family of experiments.
#
# Currently this is just the txn_overhead_vary_trans line plot
# (1x3 paper-row: btree / avltree / hash_block, vary trans). The
# heavier txn_overhead bar-chart geomean sweep is not part of the
# current paper figure set.
#
# Usage:
#   scripts/run_txn_overhead.sh             # sync + build + sweep + plot
#   scripts/run_txn_overhead.sh sync        # pull repo + sync fuse submodule only
#   scripts/run_txn_overhead.sh build       # build only
#   scripts/run_txn_overhead.sh sweep       # sweep only (assumes binaries exist)
#   scripts/run_txn_overhead.sh plot        # plot only (assumes timings exist)
#
# Wall-clock: ~10-15 min build + ~10 min sweep on a 144-core machine.

set -euo pipefail

# Locate the project root (parent of this script's directory).
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TIMINGS=$ROOT/timings
PLOTS=$ROOT/plot/plots
DATE=$(date +%m-%d-%Y)
MACHINE_TAG="144-40GHz"   # matches setup_output() naming in run_experiment.py
JOBS=$(nproc)

step_sync() {
  echo "== Fast-forwarding ustm and syncing fuse submodule =="
  # --ff-only refuses to merge if there are local commits ahead of
  # upstream; pair with a clean working tree.
  git pull --ff-only
  git submodule update --init --recursive
}

step_build() {
  echo "== Configuring and building (j=$JOBS) =="
  cmake -S . -B build
  cmake --build build -- -j "$JOBS"
}

step_sweep() {
  echo "== Running txn_overhead_vary_trans =="
  (cd benchmark && python3 run_experiment.py txn_overhead_vary_trans)
}

step_plot() {
  echo "== Staging timings and rendering plot =="
  STAGE=$(mktemp -d)
  trap "rm -rf $STAGE" EXIT
  src=$TIMINGS/txn_overhead_vary_trans_${MACHINE_TAG}_${DATE}
  if [[ ! -f $src ]]; then
    # Fall back to the newest file if today's didn't get produced
    # (e.g. plot-only invocation).
    src=$(ls -t "$TIMINGS"/txn_overhead_vary_trans_${MACHINE_TAG}_* 2>/dev/null | head -1 || true)
  fi
  if [[ -f $src ]]; then
    ln -sf "$src" "$STAGE/txn_overhead_vary_trans"
    echo "  txn_overhead_vary_trans -> $src"
  else
    echo "  ERROR: no timings file for txn_overhead_vary_trans" >&2
    exit 1
  fi
  python3 plot/plot.py "$STAGE" --plots txn_overhead -o "$STAGE/plots"
  mkdir -p "$PLOTS"
  cp "$STAGE/plots/txn_overhead_vary_trans.pdf" "$PLOTS/"
  echo "== Done. Plot at $PLOTS/txn_overhead_vary_trans.pdf =="
}

case "${1:-all}" in
  sync)  step_sync ;;
  build) step_build ;;
  sweep) step_sweep ;;
  plot)  step_plot ;;
  all)   step_sync; step_build; step_sweep; step_plot ;;
  *)     echo "usage: $0 [sync|build|sweep|plot|all]" >&2; exit 1 ;;
esac
