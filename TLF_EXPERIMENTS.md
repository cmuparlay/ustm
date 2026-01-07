# TLF Paper Experiment Scripts

Scripts to recreate the experiments from the paper:

**"TLF: Transactional Lock Fusion"** (SPAA '25)  
https://doi.org/10.1145/3694906.3743341

## Files

- [benchmark/runall_tlf.sh](benchmark/runall_tlf.sh) - Runs all experiments from the paper
- [plot/plot_tlf.py](plot/plot_tlf.py) - Generates plots from timing data

## Running Experiments

```bash
cd build/benchmark
../../benchmark/runall_tlf.sh
```

This will run all experiments and save results to `timings/tlf_<machine>_<date>.txt`

### Experiments Run

1. **Geometric Mean Performance (Figure 1)** - Varies n, update%, trans_size, zipfian
2. **Varying Size (Figures 9/10 Row 1)** - n ∈ {10³, 10⁴, 10⁵, 10⁶, 10⁷, 10⁸}
3. **Varying Thread Count (Figures 9/10 Row 2)** - p ∈ {1, 4, 8, 16, 32, 48, 64, 96, 128, 192}
4. **Varying Zipfian (Figures 9/10 Row 3)** - z ∈ {0, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99}
5. **Varying Transaction Size (Figures 9/10 Row 4)** - trans ∈ {1, 2, 4, 8, 16, 32}
6. **Varying Update Rate (Figures 9/10 Row 5)** - u ∈ {0, 1, 5, 10, 20, 50}
7. **Range Queries** - rs ∈ {1, 10, 100, 1000, 10000, 100000, 1000000}

### Default Parameters (from paper)

- n = 10⁵ (300 for linked list)
- update% = 5
- trans_size = 4
- zipfian = 0.9
- threads = 191
- rounds = 3

## Generating Plots

```bash
cd plot
python3 plot_tlf.py ../timings/<timing_file> -o output_plots --format pdf
```

### Options

```
--plots {figure1,figures9_10,range,combined,all}  # Select which plots to generate
--format {pdf,png,svg}                             # Output format
-o OUTPUT_DIR                                      # Output directory
```

### Plots Generated

- `figure1_geomean.pdf` - Normalized bar chart (Figure 1)
- `fig9_row*_*.pdf` - Skip List, Treap, B-tree plots
- `fig10_row*_*.pdf` - Linked List, AVL Tree, Leaf Tree plots
- `range_queries.pdf` - Range query throughput
- `tlf_results_combined.pdf` - Combined grid of all parameters

## Paper Experiment Details

From Section 10 of the paper:

- **Workloads**: Based on YCSB benchmark suite
- **Machine**: 96-core AWS c7i-metal (2x Intel Xeon Platinum 8488C)
- **Data Structures**: B-tree, ART Tree, AVL Tree, Leaf Tree, Treap, Skip List, Linked List, Hash Table
- **Implementations compared**: 2PLSF, 2PLSF-TLF-STM, MV-STM, MV-TLF

The scripts test the following implementations from this codebase:
- `mvustm` - Multi-version STM (MV-STM equivalent)
- `mvustm_hwstamp` - MV with hardware timestamps (MV-TLF equivalent)
- `2plsf` - Two-phase locking with starvation freedom
- `hytm` - Hardware transactional memory
