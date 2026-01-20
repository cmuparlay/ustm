# Benchamrks

Instructions to run our benchmark suite.

---

## Benchmark Scripts

### Main Script: `run_experiment.py`

Unified script for running individual or all experiments.

```bash
cd build/benchmark
../../benchmark/run_experiment.py <experiment> [options]
```

**Experiments**:

| Experiment | Description | Varying Parameter |
|------------|-------------|-------------------|
| `geomean` | Geometric mean performance (Figure 1) | n, u, trans, z (all combinations) |
| `size` | Data structure size (Figures 9/10 Row 1) | n ∈ {10³, 10⁴, 10⁵, 10⁶, 10⁷, 10⁸} |
| `threads` | Thread count scalability (Figures 9/10 Row 2) | p ∈ {1, 4, 8, 16, 32, 48, 64, 72/192} |
| `zipfian` | Access skew (Figures 9/10 Row 3) | z ∈ {0, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99} |
| `trans` | Transaction size (Figures 9/10 Row 4) | trans ∈ {1, 2, 4, 8, 16, 32} |
| `update` | Update percentage (Figures 9/10 Row 5) | u ∈ {0, 1, 5, 10, 20, 50} |
| `range` | Range query size | rs ∈ {1, 10, 100, 1000, 10000, 100000, 1000000} |
| `all` | Run all experiments | - |

**Options**:

| Option | Description |
|--------|-------------|
| `--no-hytm` | Exclude HyTM, use all available threads/sockets (191 threads) |
| `--output DIR` | Write results to `DIR/<experiment>` |
| `--help` | Show help message |

**Examples**:

```bash
# Run geometric mean experiment (default: includes HyTM, 72 threads)
../../benchmark/run_experiment.py geomean

# Run all experiments without HyTM (use all threads)
../../benchmark/run_experiment.py all --no-hytm

# Run thread scalability, output to custom directory
../../benchmark/run_experiment.py threads --output ./my_results
```

**Output**: Results saved to `timings/tlf_<experiment>_<cores>-<machine>_<date>_<suffix>`

**Notes**:
- Default mode: Includes HyTM, capped at 72 threads on 2 sockets for fair comparison
- `--no-hytm` mode: Excludes HyTM, uses all available threads/sockets
- Non-HyTM implementations use efficient single-invocation mode for geomean
- HyTM uses separate invocations per parameter combination

### Other Scripts

| Script | Description |
|--------|-------------|
| `runall.sh` | Original benchmark script (all implementations, general purpose) |

### Default Parameters (from paper Section 10)

| Parameter | Default Value | Description |
|-----------|---------------|-------------|
| n | 10⁵ (300 for list) | Data structure size |
| u | 5 | Update percentage |
| trans | 4 | Operations per transaction |
| z | 0.9 | Zipfian skew parameter |
| p | 191 | Number of threads |
| r | 3 | Number of rounds |
| t | 1 | Trial time (seconds) |

---

## Plotting Scripts

### Main Plotter: `plot/plot_tlf.py`

Generates all plots from the TLF paper.

```bash
cd plot
python3 plot_tlf.py ../timings/<timing_file> [options]
```

**Options**:

| Option | Values | Description |
|--------|--------|-------------|
| `--plots` | `figure1`, `figures9_10`, `range`, `combined`, `all` | Select which plots to generate |
| `--format` | `pdf`, `png`, `svg` | Output file format |
| `-o` | `<directory>` | Output directory for plots |
| `--normalize` | flag | Normalize throughput values |

**Examples**:

```bash
# Generate all plots as PDF
python3 plot_tlf.py ../timings/tlf_192-8488C_01-12-2026_143022 --plots all --format pdf

# Generate only Figure 1 (geomean bar chart)
python3 plot_tlf.py ../timings/tlf_192-8488C_01-12-2026_143022 --plots figure1 -o ./output

# Generate parameter variation plots as PNG
python3 plot_tlf.py ../timings/tlf_192-8488C_01-12-2026_143022 --plots figures9_10 --format png
```

**Generated Plots**:

| File | Description |
|------|-------------|
| `figure1_geomean.pdf` | Normalized bar chart (Figure 1) |
| `fig9_row1_*.pdf` | Varying size plots |
| `fig9_row2_*.pdf` | Varying thread count plots |
| `fig9_row3_*.pdf` | Varying zipfian plots |
| `fig9_row4_*.pdf` | Varying transaction size plots |
| `fig9_row5_*.pdf` | Varying update rate plots |
| `fig10_row*_*.pdf` | Additional structure plots |
| `range_queries.pdf` | Range query throughput |
| `tlf_results_combined.pdf` | Combined grid of all parameters |

### Alternative Plotters

| Script | Description | Usage |
|--------|-------------|-------|
| `plot_geomean.py` | Simple geomean bar chart | `python3 plot_geomean.py ../timings/<file>` |
| `plot_geomean_common.py` | Common configuration comparison | `python3 plot_geomean_common.py ../timings/<file>` |
| `plot_experiment.py` | Individual experiment plots | `python3 plot_experiment.py <exp_file>` |

---

## Experiments Overview

### Experiment 1: Geometric Mean Performance (Figure 1)

Tests all combinations of parameters to compute geometric mean throughput:
- **n** ∈ {10⁴, 10⁶, 10⁸}
- **update%** ∈ {0%, 5%, 50%}
- **trans_size** ∈ {1, 4, 16}
- **zipfian** ∈ {0, 0.99}

Total: 54 configurations per structure/implementation pair.

### Experiments 2-6: Parameter Variation (Figures 9/10)

Each experiment varies one parameter while keeping others at defaults:

| Experiment | Varies | Fixed at Defaults |
|------------|--------|-------------------|
| Exp 2: Size | n | u=5, trans=4, z=0.9, p=191 |
| Exp 3: Threads | p | n=10⁵, u=5, trans=4, z=0.9 |
| Exp 4: Zipfian | z | n=10⁵, u=5, trans=4, p=191 |
| Exp 5: Trans Size | trans | n=10⁵, u=5, z=0.9, p=191 |
| Exp 6: Update Rate | u | n=10⁵, trans=4, z=0.9, p=191 |

### Experiment 7: Range Queries

Tests range query performance with varying range sizes:
- **n** = 10⁶
- **update** = 100%
- **range_size** ∈ {1, 10, 100, 1000, 10000, 100000, 1000000}
- **rqp** = 50 (50% of threads doing range queries)

---

## Data Structures

| Structure | Variable | Supports Range Queries | Fuse Compatible |
|-----------|----------|------------------------|-----------------|
| B-tree | `btree` | ✓ | ✓ |
| ART Tree | `arttree` | ✓ | ✓ |
| AVL Tree | `avltree` | | ✗ (uses atomic\<int\>) |
| Leaf Tree | `leaftree` | | ✓ |
| Treap | `treap` | | ✓ |
| Skip List | `skiplist` | | ✓ |
| Linked List | `list` | | ✓ |
| Hash Table | `hash_block` | | ✗ (uses atomic\<V\>) |

## Implementations

| Implementation | Variable | Description |
|----------------|----------|-------------|
| MV-STM | `mvustm` | Multi-version STM |
| MV-TLF | `mvustm_hwstamp` | MV with hardware timestamps |
| MV-Longjmp | `mvustm_longjmp` | MV with longjmp abort |
| 2PLSF | `2plsf` | Two-phase locking with starvation freedom |
| Fuse/TLF | `fuse` | Transactional Lock Fusion (SPAA '25)¹ |
| HyTM | `hytm` | Hardware transactional memory |

¹ Fuse only supports pointer and bool atomics; excludes `avltree` and `hash_block`

---

## Output Format

Results are written in CSV-like format:

```
./btree_mvustm,u=5,trans=4,n=100000,p=191,z=0.9,pa=0.0,mops=123.456
./btree_mvustm,GEOMETRIC MEAN OF THROUGHPUTS: 87.654
```

Fields:
- `u` - Update percentage
- `trans` - Transaction size
- `n` - Data structure size
- `p` - Number of threads
- `z` - Zipfian parameter
- `pa` - Abort rate
- `mops` - Million operations per second
- `rqmops` - Range query million ops/sec (for range experiments)

---

## Requirements

**System**:
- Linux with `numactl` installed
- Multi-core processor (experiments designed for 192 threads)

**Python** (for plotting):
- Python 3.6+
- matplotlib
- numpy
