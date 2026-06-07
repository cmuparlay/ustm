#!/usr/bin/env bash
# Push-button driver that runs every benchmark experiment used in the
# paper, by delegating to the per-experiment scripts in this directory.
#
# Currently:
#   - txn_overhead family (txn_overhead_vary_trans + txn_overhead bar chart)
#   - strings_update (hash_probe Integer / Short String / Long String)
#
# All output plots land in plot/plots/.
#
# Usage:
#   scripts/run_paper_experiments.sh             # all: sync + build + sweep + plot
#   scripts/run_paper_experiments.sh sync        # pull repo + sync fuse submodule only
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

if ! [[ $PHASE =~ ^(sync|build|sweep|plot|all)$ ]]; then
  echo "usage: $0 [sync|build|sweep|plot|all]" >&2
  exit 1
fi

echo "============================================================="
echo " Paper experiments — phase: $PHASE"
echo "============================================================="

# Do the repo+submodule pull once at the wrapper level, then pass
# `build/sweep/plot` to the children so they don't each try to
# fast-forward again.
if [[ "$PHASE" == "all" || "$PHASE" == "sync" ]]; then
  cd "$ROOT"
  echo "== Fast-forwarding ustm and syncing fuse submodule =="
  git pull --ff-only
  git submodule update --init --recursive
fi

if [[ "$PHASE" == "sync" ]]; then
  echo "============================================================="
  echo " Sync done."
  echo "============================================================="
  exit 0
fi

# If the wrapper got "all", run build/sweep/plot in each child so the
# children don't re-sync.
CHILD_PHASE=$PHASE
if [[ "$PHASE" == "all" ]]; then
  CHILD_PHASE_LIST=(build sweep plot)
else
  CHILD_PHASE_LIST=("$PHASE")
fi

echo
echo "[1/2] txn_overhead family"
for p in "${CHILD_PHASE_LIST[@]}"; do
  "$SCRIPTS/run_txn_overhead.sh" "$p"
done

echo
echo "[2/2] strings_update"
for p in "${CHILD_PHASE_LIST[@]}"; do
  "$SCRIPTS/run_strings.sh" "$p"
done

echo
echo "============================================================="
echo " All experiments done. Plots in plot/plots/."
echo "============================================================="
