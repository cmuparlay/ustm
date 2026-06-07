#!/usr/bin/env bash
# Push-button driver that runs every benchmark experiment used in the
# paper, by delegating to the per-experiment scripts in this directory.
#
# Currently:
#   - txn_overhead family (vary_trans_focus + txn_overhead bar chart)
#   - strings_update (hash_probe Integer / Short String / Long String)
#
# All output plots land in plot/plots/.
#
# Usage:
#   scripts/run_paper_experiments.sh             # all: build + sweep + plot
#   scripts/run_paper_experiments.sh build       # build only
#   scripts/run_paper_experiments.sh sweep       # sweep only
#   scripts/run_paper_experiments.sh plot        # plot only
#
# Wall-clock: ~90 min total on a 144-core machine (most of it
# txn_overhead's full geomean sweep).

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
SCRIPTS=$ROOT/scripts
PHASE=${1:-all}

if ! [[ $PHASE =~ ^(build|sweep|plot|all)$ ]]; then
  echo "usage: $0 [build|sweep|plot|all]" >&2
  exit 1
fi

echo "============================================================="
echo " Paper experiments — phase: $PHASE"
echo "============================================================="

echo
echo "[1/2] txn_overhead family"
"$SCRIPTS/run_txn_overhead.sh" "$PHASE"

echo
echo "[2/2] strings_update"
"$SCRIPTS/run_strings.sh" "$PHASE"

echo
echo "============================================================="
echo " All experiments done. Plots in plot/plots/."
echo "============================================================="
