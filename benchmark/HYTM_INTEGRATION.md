# HyTm Integration with USTM Benchmarks

This integration allows you to run your existing USTM benchmarks using HyTm as the underlying STM implementation instead of USTM.

## Usage

To use HyTm as the STM backend when building your benchmarks:

```bash
# Configure with HyTm backend
cmake -DUSE_HYTM_BACKEND=ON ..

# Build as usual
make avltree_mvustm  # This will now use HyTm instead of USTM
make tpcc_btree_mvustm  # TPCC benchmark with HyTm backend
```

## What this does

- Replaces the USTM STM implementation with HyTm (NVRAM disabled for fair comparison)
- Uses HyTm's volatile STM mode: `phytm1_fgl_colocated_locks_no_pmem`
- Maps USTM's transactional API to HyTm's `tx_field<T>` interface
- Maintains the same benchmark code but uses HyTm for transaction management

## Benchmarking HyTm vs USTM

```bash
# Build with USTM (default)
cmake ..
make avltree_mvustm
./avltree_mvustm -n 100000 -t 5

# Build with HyTm backend  
cmake -DUSE_HYTM_BACKEND=ON ..
make avltree_mvustm  # Same benchmark name, different STM backend
./avltree_mvustm -n 100000 -t 5
```

## Configuration

The integration uses HyTm with:
- NVRAM disabled (`DISABLE_PMEM`)
- Colocated locks variant (`phytm1_fgl_colocated_locks_no_pmem`)
- Pure STM mode for fair comparison with USTM

## Requirements

- libnuma-dev (if available, will be linked automatically)
- The HyTm source code in `other/ptmbench/`

## Files Modified

- `benchmark/transactions.h` - Added HyTm backend option
- `benchmark/glue/hytm_glue.h` - HyTm adapter implementation
- `benchmark/CMakeLists.txt` - Build configuration for HyTm
- `user_gstats_handler.h` - Minimal statistics stub for HyTm

This allows you to run the exact same benchmark workloads with both STM systems for direct performance comparison.