#!/bin/bash

#####################################################################
# TLF Paper - Run All Experiments
# Runs exp1-exp7 with logging and error reporting
#
# Usage:
#   ./run_all_tlf.sh              # Run all with HYTM_MODE=true (default)
#   HYTM_MODE=false ./run_all_tlf.sh  # Run without hytm
#####################################################################

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/../build/benchmark"

# Inherit HYTM_MODE or default to true
export HYTM_MODE=${HYTM_MODE:-true}

# Create logs directory
procs=$(nproc)
today=$(date +"%m-%d-%Y_%H%M%S")
machine=$(lscpu | grep 'Model name' | rev | cut -c 1-5 | rev | tr -d '[:space:]')

if [ "$HYTM_MODE" = "true" ]; then
    mode_suffix="_hytm"
else
    mode_suffix=""
fi

LOG_DIR="${SCRIPT_DIR}/../logs/tlf_${procs}-${machine}_${today}${mode_suffix}"
mkdir -p "${LOG_DIR}"

MAIN_LOG="${LOG_DIR}/main.log"
ERROR_LOG="${LOG_DIR}/errors.log"

# Experiments to run
EXPERIMENTS=(
    "exp1_geomean.sh:Geometric Mean (Figure 1)"
    "exp2_vary_size.sh:Varying Size (Row 1)"
    "exp3_vary_threads.sh:Varying Threads (Row 2)"
    "exp4_vary_zipfian.sh:Varying Zipfian (Row 3)"
    "exp5_vary_trans.sh:Varying Transaction Size (Row 4)"
    "exp6_vary_update.sh:Varying Update Rate (Row 5)"
    "exp7_range_queries.sh:Range Queries"
)

log() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] $1"
    echo "$msg"
    echo "$msg" >> "${MAIN_LOG}"
}

log_error() {
    local msg="[$(date '+%Y-%m-%d %H:%M:%S')] ERROR: $1"
    echo "$msg" >&2
    echo "$msg" >> "${MAIN_LOG}"
    echo "$msg" >> "${ERROR_LOG}"
}

# Header
log "=========================================="
log "TLF Paper - Running All Experiments"
log "=========================================="
log "HYTM_MODE: ${HYTM_MODE}"
log "Build directory: ${BUILD_DIR}"
log "Log directory: ${LOG_DIR}"
log "Machine: ${machine}"
log "Processors: ${procs}"
log "$(lscpu | grep 'Model name')"
log "=========================================="

# Check build directory exists
if [ ! -d "${BUILD_DIR}" ]; then
    log_error "Build directory not found: ${BUILD_DIR}"
    log_error "Please run 'cmake .. && make' in the build directory first."
    exit 1
fi

cd "${BUILD_DIR}"

# Track results
declare -A RESULTS
TOTAL=0
PASSED=0
FAILED=0

# Run each experiment
for exp_entry in "${EXPERIMENTS[@]}"; do
    script="${exp_entry%%:*}"
    desc="${exp_entry##*:}"
    exp_log="${LOG_DIR}/${script%.sh}.log"
    
    TOTAL=$((TOTAL + 1))
    
    log ""
    log "----------------------------------------"
    log "Starting: ${desc}"
    log "Script: ${script}"
    log "Log: ${exp_log}"
    log "----------------------------------------"
    
    start_time=$(date +%s)
    
    if [ ! -f "${SCRIPT_DIR}/${script}" ]; then
        log_error "Script not found: ${SCRIPT_DIR}/${script}"
        RESULTS["${script}"]="FAILED (script not found)"
        FAILED=$((FAILED + 1))
        continue
    fi
    
    # Run experiment with logging
    if "${SCRIPT_DIR}/${script}" > "${exp_log}" 2>&1; then
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        log "COMPLETED: ${desc} (${duration}s)"
        RESULTS["${script}"]="PASSED (${duration}s)"
        PASSED=$((PASSED + 1))
    else
        exit_code=$?
        end_time=$(date +%s)
        duration=$((end_time - start_time))
        log_error "FAILED: ${desc} (exit code: ${exit_code}, ${duration}s)"
        log_error "See log: ${exp_log}"
        
        # Capture last 20 lines of output for error context
        log_error "Last 20 lines of output:"
        tail -20 "${exp_log}" >> "${ERROR_LOG}" 2>/dev/null || true
        
        RESULTS["${script}"]="FAILED (exit: ${exit_code}, ${duration}s)"
        FAILED=$((FAILED + 1))
    fi
done

# Summary
log ""
log "=========================================="
log "SUMMARY"
log "=========================================="
log "Total: ${TOTAL}, Passed: ${PASSED}, Failed: ${FAILED}"
log ""

for exp_entry in "${EXPERIMENTS[@]}"; do
    script="${exp_entry%%:*}"
    desc="${exp_entry##*:}"
    status="${RESULTS[$script]:-UNKNOWN}"
    log "  ${desc}: ${status}"
done

log ""
log "Logs saved to: ${LOG_DIR}"
log "Data files saved to: ../../timings/"
log "=========================================="

# Exit with error if any failed
if [ ${FAILED} -gt 0 ]; then
    log_error "${FAILED} experiment(s) failed. Check ${ERROR_LOG} for details."
    exit 1
fi

log "All experiments completed successfully!"
exit 0
