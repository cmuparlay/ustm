# HyTm Integration with USTM Benchmarks

HyTm benchmarks are built alongside other STM implementations using the `HyTm` define.

## Usage

HyTm benchmarks are built automatically with the standard build process:

```bash
cd build
cmake ..
make -j

# Run HyTm benchmark
./benchmark/btree_hytm -n 100000 -t 5
```

## What this does

- Uses HyTm's volatile STM mode: `phytm1_fgl_colocated_locks_no_pmem`
- NVRAM disabled for fair comparison with other STM implementations
- Integrates via `benchmark/glue/other_glue.h` with the `HyTm` define

## Benchmarking HyTm vs USTM

```bash
# Run USTM benchmark
./benchmark/btree_mvustm -n 100000 -t 5

# Run HyTm benchmark
./benchmark/btree_hytm -n 100000 -t 5
```

## Configuration

The integration uses HyTm with:
- NVRAM disabled (`DISABLE_PMEM`)
- Colocated locks variant (`phytm1_fgl_colocated_locks_no_pmem`)
- Pure STM mode for fair comparison with USTM

## Requirements

- libnuma-dev (linked automatically)
- The HyTm source code in `other/ptmbench/`

## Files

- `benchmark/glue/other_glue.h` - HyTm adapter (with `HyTm` define)
- `benchmark/CMakeLists.txt` - Build configuration for HyTm
- `user_gstats_handler.h` - Statistics stub for HyTm