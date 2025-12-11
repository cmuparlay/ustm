#!/usr/bin/env python3
"""
Plot HyTM benchmark results from timing files.
Generates plots comparing throughput across different data structures,
update percentages, zipfian parameters, and transaction sizes.
"""

import re
import sys
import argparse
from pathlib import Path
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

def parse_timing_file(filepath):
    """Parse a HyTM timing file and extract benchmark results."""
    results = []
    current_struct = None
    geomean_data = {}
    
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            
            # Structure header
            if line and not line.startswith('./') and not line.startswith('n:') and \
               not line.startswith('Date:') and not line.startswith('Model') and \
               not line.startswith('Git') and not line.startswith('Compiler:') and \
               not line.startswith('OS:') and not line.startswith('Hugepage:') and \
               not line.startswith('Threads') and not line.startswith('tpcc') and \
               not line.startswith('range_size:') and 'range queries' not in line:
                current_struct = line
                continue
            
            # Individual run result
            match = re.search(r'\./(\w+)_hytm,u=(\d+),trans=(\d+),n=(\d+),p=(\d+),z=([\d.]+),pa=([\d.]+),mops=([\d.]+)', line)
            if match:
                struct, u, trans, n, p, z, pa, mops = match.groups()
                results.append({
                    'structure': struct,
                    'update_percent': int(u),
                    'trans_size': int(trans),
                    'n': int(n),
                    'threads': int(p),
                    'zipfian': float(z),
                    'abort_percent': float(pa),
                    'mops': float(mops)
                })
            
            # Geometric mean summary
            match = re.search(r'n:\s*(\d+),\s*geomean throughput:\s*([\d.]+)', line)
            if match and current_struct:
                n, gmean = match.groups()
                key = (current_struct, int(n))
                geomean_data[key] = float(gmean)
    
    return results, geomean_data

def plot_geomean_by_size(geomean_data, output_dir):
    """Plot geometric mean throughput vs dataset size for each structure."""
    # Group by structure
    by_struct = defaultdict(list)
    for (struct, n), gmean in geomean_data.items():
        by_struct[struct].append((n, gmean))
    
    plt.figure(figsize=(12, 7))
    for struct, data in sorted(by_struct.items()):
        data = sorted(data, key=lambda x: x[0])
        sizes = [d[0] for d in data]
        gmeans = [d[1] for d in data]
        plt.plot(sizes, gmeans, marker='o', linewidth=2, markersize=8, label=struct)
    
    plt.xscale('log')
    plt.xlabel('Dataset Size (n)', fontsize=12)
    plt.ylabel('Geometric Mean Throughput (Mops/s)', fontsize=12)
    plt.title('HyTM Throughput by Dataset Size', fontsize=14)
    plt.legend(fontsize=10)
    plt.grid(True, alpha=0.3)
    plt.tight_layout()
    
    filename = 'geomean_by_size.png'
    plt.savefig(output_dir / filename, dpi=150)
    plt.close()
    print(f"Generated: {filename}")

def plot_abort_rate(results, output_dir):
    """Plot abort rate vs update percentage."""
    # Group by structure, n, trans_size, zipfian
    grouped = defaultdict(list)
    for r in results:
        if r['abort_percent'] > 0:  # Only plot if there are aborts
            key = (r['structure'], r['n'], r['trans_size'], r['zipfian'])
            grouped[key].append(r)
    
    for (struct, n, trans, z), data in grouped.items():
        if len(data) < 2:
            continue
        
        data = sorted(data, key=lambda x: x['update_percent'])
        update_percents = [d['update_percent'] for d in data]
        abort_rates = [d['abort_percent'] for d in data]
        
        plt.figure(figsize=(10, 6))
        plt.plot(update_percents, abort_rates, marker='s', linewidth=2, 
                markersize=8, color='crimson')
        plt.xlabel('Update Percentage', fontsize=12)
        plt.ylabel('Abort Rate (%)', fontsize=12)
        plt.title(f'{struct} Abort Rate (n={n:,}, trans={trans}, z={z})', fontsize=14)
        plt.grid(True, alpha=0.3)
        plt.tight_layout()
        
        filename = f'{struct}_n{n}_trans{trans}_z{z}_aborts.png'
        plt.savefig(output_dir / filename, dpi=150)
        plt.close()
        print(f"Generated: {filename}")

def main():
    parser = argparse.ArgumentParser(description='Plot HyTM benchmark results')
    parser.add_argument('input_file', type=str, 
                       help='Path to HyTM timing file')
    parser.add_argument('-o', '--output-dir', type=str, default='plots_hytm',
                       help='Output directory for plots (default: plots_hytm)')
    parser.add_argument('--plot-types', nargs='+', 
                       choices=['geomean', 'aborts', 'all'],
                       default=['geomean'],
                       help='Types of plots to generate (default: geomean)')
    
    args = parser.parse_args()
    
    input_path = Path(args.input_file)
    if not input_path.exists():
        print(f"Error: File '{args.input_file}' not found")
        return 1
    
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)
    
    print(f"Parsing {input_path}...")
    results, geomean_data = parse_timing_file(input_path)
    
    if not results:
        print("Warning: No benchmark results found in file")
        return 1
    
    print(f"Found {len(results)} benchmark results")
    print(f"Found {len(geomean_data)} geometric mean summaries")
    
    plot_types = args.plot_types
    if 'all' in plot_types:
        plot_types = ['geomean', 'aborts']
    
    if 'geomean' in plot_types and geomean_data:
        print("\nGenerating geometric mean plots...")
        plot_geomean_by_size(geomean_data, output_dir)
    
    if 'aborts' in plot_types:
        print("\nGenerating abort rate plots...")
        plot_abort_rate(results, output_dir)
    
    print(f"\nAll plots saved to {output_dir}/")
    return 0

if __name__ == '__main__':
    sys.exit(main())
