#!/usr/bin/env python3
"""
TLF Paper Plotting Scripts - Separate plotters for each experiment type.
Each experiment has its own data file and plotting function.
"""

import re
import sys
import argparse
from pathlib import Path
from collections import defaultdict
import matplotlib.pyplot as plt
import numpy as np

# Consistent styling
IMPL_COLORS = {
    'mvustm': '#1f77b4',
    'mvustm_hwstamp': '#2ca02c',
    'mvustm_lzstamp': '#ff7f0e',
    'ustm_longjmp': '#d62728',
    'mvustm_longjmp': '#9467bd',
    'mvustm_trythrow': '#8c564b',
    'mvustm_unprotected': '#e377c2',
    '2plsf': '#7f7f7f',
    'hytm': '#bcbd22',
    'tiny': '#17becf',
}

IMPL_LABELS = {
    'mvustm': 'MV-STM',
    'mvustm_hwstamp': 'MV-TLF',
    'mvustm_longjmp': 'MV-Longjmp',
    '2plsf': '2PLSF',
    'hytm': 'HyTM',
}

STRUCT_LABELS = {
    'btree': 'B-tree',
    'arttree': 'ART Tree',
    'avltree': 'AVL Tree',
    'leaftree': 'Leaf Tree',
    'treap': 'Treap',
    'skiplist': 'Skip List',
    'list': 'Linked List',
    'hash_block': 'Hash Table',
}

IMPL_ORDER = ['mvustm', 'mvustm_hwstamp', 'mvustm_longjmp', '2plsf', 'hytm']


def parse_geomean_file(filepath):
    """Parse experiment 1 (geomean) data file."""
    data = {}  # (struct, impl) -> geomean
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            match = re.match(r'^\./(\w+)_(\w+),GEOMETRIC MEAN:\s*([\d.eE+-]+)', line)
            if match:
                struct, impl, gmean = match.groups()
                data[(struct, impl)] = float(gmean)
    return data


def parse_varying_file(filepath, param_name):
    """Parse parameter-varying experiment data file."""
    data = defaultdict(lambda: defaultdict(list))  # data[struct][impl] = [(param_val, mops), ...]
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            # Format: struct,impl,param=val,mops=val
            match = re.match(rf'^(\w+),(\w+),{param_name}=([\d.]+),mops=([\d.eE+-]+)', line)
            if match:
                struct, impl, param_val, mops = match.groups()
                data[struct][impl].append((float(param_val), float(mops)))
    return data


def parse_range_file(filepath):
    """Parse range query experiment data file."""
    data = defaultdict(lambda: defaultdict(list))
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            # Format: struct,impl,rs=val,rqmops=val,mops=val
            match = re.match(r'^(\w+),(\w+),rs=([\d.]+),rqmops=([\d.eE+-]+)', line)
            if match:
                struct, impl, rs, rqmops = match.groups()
                data[struct][impl].append((float(rs), float(rqmops)))
    return data


def plot_figure1(data, output_path):
    """Plot Figure 1: Normalized geomean bar chart."""
    structures = sorted(set(s for s, _ in data.keys()))
    impls_found = sorted(set(i for _, i in data.keys()))
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    # Normalize per structure
    normalized = defaultdict(dict)
    for struct in structures:
        max_val = max((data.get((struct, impl), 0) for impl in impls), default=1)
        if max_val > 0:
            for impl in impls:
                if (struct, impl) in data:
                    normalized[struct][impl] = data[(struct, impl)] / max_val
    
    fig, ax = plt.subplots(figsize=(12, 5))
    x = np.arange(len(structures))
    width = 0.8 / len(impls)
    
    for i, impl in enumerate(impls):
        values = [normalized[s].get(impl, 0) for s in structures]
        offset = (i - len(impls)/2 + 0.5) * width
        color = IMPL_COLORS.get(impl, '#333')
        label = IMPL_LABELS.get(impl, impl)
        ax.bar(x + offset, values, width, label=label, color=color)
    
    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)
    ax.set_xlabel('Data Structure', fontsize=11)
    ax.set_ylabel('Normalized Throughput', fontsize=11)
    ax.set_title('Figure 1: Normalized Geomean Throughput (higher is better)', fontsize=12)
    ax.set_xticks(x)
    ax.set_xticklabels([STRUCT_LABELS.get(s, s) for s in structures], rotation=45, ha='right')
    ax.legend(loc='upper right', fontsize=9)
    ax.grid(axis='y', alpha=0.3)
    ax.set_ylim(0, 1.15)
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_path}")
    plt.close()


def plot_varying_param(data, param_name, param_label, output_path, log_x=False):
    """Plot parameter-varying experiment (line plots per structure)."""
    structures = sorted(data.keys())
    if not structures:
        print(f"No data to plot for {param_name}")
        return
    
    ncols = min(3, len(structures))
    nrows = (len(structures) + ncols - 1) // ncols
    
    fig, axes = plt.subplots(nrows, ncols, figsize=(5*ncols, 4*nrows), squeeze=False)
    
    impls_found = set()
    for struct_data in data.values():
        impls_found.update(struct_data.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    for idx, struct in enumerate(structures):
        row, col = idx // ncols, idx % ncols
        ax = axes[row, col]
        
        for impl in impls:
            if impl not in data[struct]:
                continue
            points = sorted(data[struct][impl])
            if not points:
                continue
            x_vals, y_vals = zip(*points)
            color = IMPL_COLORS.get(impl, '#333')
            label = IMPL_LABELS.get(impl, impl)
            ax.plot(x_vals, y_vals, marker='o', linewidth=2, markersize=6, color=color, label=label)
        
        ax.set_xlabel(param_label, fontsize=10)
        ax.set_ylabel('Throughput (MOps/s)', fontsize=10)
        ax.set_title(STRUCT_LABELS.get(struct, struct), fontsize=11)
        ax.grid(True, alpha=0.3)
        if log_x:
            ax.set_xscale('log')
        ax.legend(fontsize=8)
    
    # Hide empty subplots
    for idx in range(len(structures), nrows * ncols):
        row, col = idx // ncols, idx % ncols
        axes[row, col].axis('off')
    
    plt.tight_layout()
    plt.savefig(output_path, dpi=150, bbox_inches='tight')
    print(f"Saved: {output_path}")
    plt.close()


def main():
    parser = argparse.ArgumentParser(description='Plot TLF experiment results')
    parser.add_argument('experiment', choices=['geomean', 'size', 'threads', 'zipfian', 'trans', 'update', 'range'],
                       help='Which experiment to plot')
    parser.add_argument('input_file', help='Path to data file')
    parser.add_argument('-o', '--output', default=None, help='Output file path')
    parser.add_argument('--format', default='png', choices=['png', 'pdf', 'svg'])
    
    args = parser.parse_args()
    
    input_path = Path(args.input_file)
    if not input_path.exists():
        print(f"Error: {input_path} not found")
        return 1
    
    output_path = args.output or f"{args.experiment}_plot.{args.format}"
    
    plt.switch_backend('Agg')
    
    if args.experiment == 'geomean':
        data = parse_geomean_file(input_path)
        plot_figure1(data, output_path)
    elif args.experiment == 'size':
        data = parse_varying_file(input_path, 'n')
        plot_varying_param(data, 'n', 'Dataset Size (n)', output_path, log_x=True)
    elif args.experiment == 'threads':
        data = parse_varying_file(input_path, 'p')
        plot_varying_param(data, 'p', 'Number of Threads', output_path)
    elif args.experiment == 'zipfian':
        data = parse_varying_file(input_path, 'z')
        plot_varying_param(data, 'z', 'Zipfian Parameter', output_path)
    elif args.experiment == 'trans':
        data = parse_varying_file(input_path, 'trans')
        plot_varying_param(data, 'trans', 'Operations per Transaction', output_path)
    elif args.experiment == 'update':
        data = parse_varying_file(input_path, 'u')
        plot_varying_param(data, 'u', 'Update Rate (%)', output_path)
    elif args.experiment == 'range':
        data = parse_range_file(input_path)
        plot_varying_param(data, 'rs', 'Range Size', output_path, log_x=True)
    
    return 0


if __name__ == '__main__':
    sys.exit(main())
