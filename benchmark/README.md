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
| `geomean` | Geometric mean performance| n, u, trans, z (all combinations) |
| `size` | Data structure size | n ∈ {10³, 10⁴, 10⁵, 10⁶, 10⁷, 10⁸} |
| `threads` | Thread count scalability | p ∈ {1, 4, 8, 16, 32, 48, 64, 96, 128, 144} (72 with `--hytm`) |
| `zipfian` | Access skew | z ∈ {0, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99} |
| `trans` | Transaction size | trans ∈ {1, 2, 4, 8, 16, 32} |
| `update` | Update percentage | u ∈ {0, 1, 5, 10, 20, 50} |
| `range` | Range query size | rs ∈ {1, 10, 100, 1000, 10000, 100000, 1000000} |
| `all` | Run all experiments | - |

**Options**:

| Option | Description |
|--------|-------------|
| `--hytm` | Include HyTM (limits to 72 threads on 2 sockets) |
| `--small` | Restrict to btree, hash_block, and list (geomean uses runall.sh parameters) |
| `--output DIR` | Write results to `DIR/<experiment>` |
| `--help` | Show help message |

**Examples**:

```bash
# Run geometric mean experiment (default: excludes HyTM, uses all threads)
../../benchmark/run_experiment.py geomean

# Run all experiments with HyTM (limited to 72 threads)
../../benchmark/run_experiment.py all --hytm

# Run small subset (btree, hash_block, list only)
../../benchmark/run_experiment.py geomean --small

# Run thread scalability, output to custom directory
../../benchmark/run_experiment.py threads --output ./my_results
```

**Notes**:
- Default mode: Excludes HyTM, uses all available threads/sockets
- `--hytm` mode: Includes HyTM, capped at 72 threads on 2 sockets for fair comparison
- `--small` mode: Runs only btree, hash_block, and list; geomean uses runall.sh parameters
- Non-HyTM implementations use efficient single-invocation mode for geomean
- HyTM uses separate invocations per parameter combination
- Timing information is displayed for each experiment

### Default Parameters (from paper Section 10)

| Parameter | Default Value | Description |
|-----------|---------------|-------------|
| n | 10⁵ (300 for list) | Data structure size |
| u | 5 | Update percentage |
| trans | 4 | Operations per transaction |
| z | 0.9 | Zipfian skew parameter |
| p | 144 (72 with `--hytm`) | Number of threads |
| r | 1 | Number of rounds |
| t | 1 | Trial time (seconds) |

---

## Plotting Scripts

### Plotter: `plot/plot.py`

Generates all plots.

```bash
cd plot
python3 plot.py ../timings/<timing_file> [options]
```

**Options**:

| Option | Values | Description |
|--------|--------|-------------|
| `--plots` | `geomean`, `varying`, `range`, `combined`, `all` | Select which plots to generate |
| `--format` | `pdf`, `png`, `svg` | Output file format |
| `-o` | `<directory>` | Output directory for plots |
| `--normalize` | flag | Normalize throughput values |

**Examples**:

```bash
# Generate all plots as PDF
python3 plot.py ../timings/192-8488C_01-12-2026_143022 --plots all --format pdf

# Generate only Figure 1 (geomean bar chart)
python3 plot.py ../timings/92-8488C_01-12-2026_143022 --plots geomean -o ./output

# Generate parameter variation plots as PNG
python3 plot.py ../timings/192-8488C_01-12-2026_143022 --plots varying --format png
```

## Experiments Overview

### Geometric Mean Performance

Tests all combinations of parameters to compute geometric mean throughput:
- **n** ∈ {10⁴, 10⁶, 10⁸} (default) or {10⁵, 10⁷} (with `--small`)
- **update%** ∈ {0%, 5%, 50%}
- **trans_size** ∈ {1, 16} (default) or {4} (with `--small`)
- **zipfian** ∈ {0, 0.99}

Total: 36 configurations per structure/implementation pair (default), 12 with `--small`.

### Parameter Variation

Each experiment varies one parameter while keeping others at defaults:

| Experiment | Varies | Fixed at Defaults |
|------------|--------|-------------------|
| Exp 2: Size | n | u=5, trans=4, z=0.9, p=144/72 |
| Exp 3: Threads | p | n=10⁵, u=5, trans=4, z=0.9 |
| Exp 4: Zipfian | z | n=10⁵, u=5, trans=4, p=144/72 |
| Exp 5: Trans Size | trans | n=10⁵, u=5, z=0.9, p=144/72 |
| Exp 6: Update Rate | u | n=10⁵, trans=4, z=0.9, p=144/72 |

### Range Queries

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
| Fuse/TLF | `fuse` | Transactional Lock Fusion |
| HyTM | `hytm` | HyTm |

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
