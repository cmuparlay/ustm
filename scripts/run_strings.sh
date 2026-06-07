#!/usr/bin/env bash
# Push-button driver for the strings_update experiment.
#
# Compares three hash_probe variants over update rate (0..100%):
#   - hash_probe_mvustm                  (long, 8-byte integer values)
#   - hash_probe_string_mvustm           (short string values, ~1 char)
#   - hash_probe_string_mvustm -bigval   (long string values, ~20 chars)
#
# Builds only the two binaries the sweep needs, runs the sweep, then
# regenerates strings_update.pdf under plot/plots/.
#
# Usage:
#   scripts/run_strings.sh             # sync + build + sweep + plot
#   scripts/run_strings.sh sync        # pull repo + sync fuse submodule only
#   scripts/run_strings.sh build       # build only
#   scripts/run_strings.sh sweep       # sweep only
#   scripts/run_strings.sh plot        # plot only (uses most recent timings file)
#
# Wall-clock: ~1-2 min build + ~2 min sweep on a 144-core machine.

set -euo pipefail

ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
TIMINGS=$ROOT/timings
PLOTS=$ROOT/plot/plots
DATE=$(date +%m-%d-%Y)
MACHINE_TAG="144-40GHz"
JOBS=$(nproc)

step_sync() {
  echo "== Fast-forwarding ustm and syncing fuse submodule =="
  git pull --ff-only
  git submodule update --init --recursive
}

step_build() {
  echo "== Configuring and building hash_probe targets (j=$JOBS) =="
  cmake -S . -B build
  cmake --build build --target hash_probe_mvustm hash_probe_string_mvustm -- -j "$JOBS"
}

step_sweep() {
  echo "== Running strings_update =="
  (cd benchmark && python3 run_experiment.py strings_update)
}

step_plot() {
  echo "== Staging timings and rendering plot =="
  STAGE=$(mktemp -d)
  trap "rm -rf $STAGE" EXIT
  src=$TIMINGS/strings_update_${MACHINE_TAG}_${DATE}
  if [[ ! -f $src ]]; then
    src=$(ls -t "$TIMINGS"/strings_update_${MACHINE_TAG}_* 2>/dev/null | head -1 || true)
  fi
  if [[ -f $src ]]; then
    ln -sf "$src" "$STAGE/strings_update"
    echo "  strings_update -> $src"
  else
    echo "  ERROR: no timings file for strings_update" >&2
    exit 1
  fi
  python3 plot/plot.py "$STAGE" --plots strings -o "$STAGE/plots"
  mkdir -p "$PLOTS"
  cp "$STAGE/plots/strings_update.pdf" "$PLOTS/"
  echo "== Done. Plot at $PLOTS/strings_update.pdf =="
}

case "${1:-all}" in
  sync)  step_sync ;;
  build) step_build ;;
  sweep) step_sweep ;;
  plot)  step_plot ;;
  all)   step_sync; step_build; step_sweep; step_plot ;;
  *)     echo "usage: $0 [sync|build|sweep|plot|all]" >&2; exit 1 ;;
esac
