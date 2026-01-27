#!/usr/bin/env python3
"""
Ploting Script

Generates the following plots:
- Normalized geomean throughput bar chart
- Varying size
- Varying thread count  
- Varying zipfian
- Varying transaction size
- Varying update rate
"""

import re
import sys
import argparse
import os
from pathlib import Path
from collections import defaultdict
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Map impl -> color
IMPL_COLORS = {
    'mvustm': '#1f77b4',
    'mvustm_hwstamp': '#2ca02c',
    'mvustm_lzstamp': '#ff7f0e',
    'ustm': '#d62728',
    'ustm_longjmp': '#d62728',
    'ustm_longjmp_hwstamp': '#e377c2',
    'mvustm_longjmp': '#9467bd',
    'mvustm_trythrow': '#8c564b',
    'mvustm_unprotected': '#17becf',
    '2plsf': '#7f7f7f',
    'hytm': '#bcbd22',
    'tiny': '#ff9896',
    'fuse': '#e41a1c',
}

IMPL_LABELS = {
    'mvustm': 'MVUSTM',
    'mvustm_hwstamp': 'MVUSTM-HWStamp',
    'mvustm_lzstamp': 'MVUSTM-LZStamp',
    'ustm': 'USTM',
    'ustm_longjmp': 'USTM-Longjmp',
    'ustm_longjmp_hwstamp': 'USTM-Longjmp-HWStamp',
    'mvustm_longjmp': 'MVUSTM-Longjmp',
    'mvustm_trythrow': 'MVUSTM-TryThrow',
    'mvustm_unprotected': 'MVUSTM-Unprotected',
    '2plsf': '2PLSF',
    'hytm': 'HyTM',
    'tiny': 'TinySTM',
    'fuse': 'TLF',
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
    'hash_probe': 'Hash Probe',
}

# Implementation ordering (for consistent legend order)
IMPL_ORDER = [
    'mvustm', 'mvustm_hwstamp', 'mvustm_lzstamp', 
    'ustm_longjmp', 'mvustm_longjmp', 'mvustm_trythrow',
    'mvustm_unprotected', '2plsf', 'hytm', 'tiny', 'fuse'
]

# Known implementations (longer matches first)
KNOWN_IMPLS = [
    'ustm_longjmp_hwstamp', 'mvustm_lzstamp', 'mvustm_hwstamp', 'mvustm_longjmp', 
    'mvustm_trythrow', 'mvustm_unprotected', 'ustm_longjmp', 'mvustm', 'ustm',
    '2plsf', 'hytm', 'tiny', 'fuse'
]


def parse_structure_impl(name):
    """Parse structure_impl string, matching known implementations from the end."""
    for impl in KNOWN_IMPLS:
        if name.endswith('_' + impl):
            struct = name[:-(len(impl) + 1)]
            return struct, impl
    parts = name.split('_', 1)
    if len(parts) == 2:
        return parts[0], parts[1]
    return name, 'unknown'


def parse_result_line(line):
    """Parse a result line and extract all parameters."""
    result = {}
    
    # Match compact format: struct,impl,params (e.g., btree,mvustm,p=1,mops=6.4)
    compact_match = re.match(r'^(\w+),(\w+),(.+)$', line)
    if compact_match:
        struct = compact_match.group(1)
        impl = compact_match.group(2)
        params_str = compact_match.group(3)
        
        # Verify it looks like params (contains =)
        if '=' in params_str:
            result['structure'] = struct
            result['impl'] = impl
            
            # Parse parameters
            param_patterns = {
                'u': r'u=(\d+)',
                'trans': r'trans=(\d+)',
                'n': r'n=(\d+)',
                'p': r'p=(\d+)',
                'z': r'z=([\d.]+)',
                'pa': r'pa=([\d.]+)',
                'mops': r'mops=([\d.eE+-]+)',
                'rs': r'rs=(\d+)',
                'rqp': r'rqp=(\d+)',
                'rqmops': r'rqmops=([\d.eE+-]+)',
            }
            
            for param, pattern in param_patterns.items():
                pmatch = re.search(pattern, params_str)
                if pmatch:
                    val = pmatch.group(1)
                    if param in ['u', 'trans', 'n', 'p', 'rs', 'rqp']:
                        result[param] = int(val)
                    else:
                        result[param] = float(val)
            
            return result if 'mops' in result else None
    
    # Match format: ./struct_impl,params,mops=value
    # Also handle prefixed format: ./struct_impl,result_line
    match = re.match(r'^(?:\./)?(\S+?),(.+)$', line)
    if not match:
        return None
    
    name = match.group(1)
    params_str = match.group(2)
    
    # Handle case where the line is prefixed with ./name, followed by ./name again
    if params_str.startswith('./'):
        # Format: ./struct_impl,./struct_impl,u=0,...
        inner_match = re.match(r'^\.\/(\S+?),(.+)$', params_str)
        if inner_match:
            name = inner_match.group(1)
            params_str = inner_match.group(2)
    
    struct, impl = parse_structure_impl(name)
    result['structure'] = struct
    result['impl'] = impl
    
    # Parse parameters
    param_patterns = {
        'u': r'u=(\d+)',
        'trans': r'trans=(\d+)',
        'n': r'n=(\d+)',
        'p': r'p=(\d+)',
        'z': r'z=([\d.]+)',
        'pa': r'pa=([\d.]+)',
        'mops': r'mops=([\d.eE+-]+)',
        'rs': r'rs=(\d+)',
        'rqp': r'rqp=(\d+)',
        'rqmops': r'rqmops=([\d.eE+-]+)',
    }
    
    for param, pattern in param_patterns.items():
        pmatch = re.search(pattern, params_str)
        if pmatch:
            val = pmatch.group(1)
            if param in ['u', 'trans', 'n', 'p', 'rs', 'rqp']:
                result[param] = int(val)
            else:
                result[param] = float(val)
    
    return result if 'mops' in result else None


def parse_geomean_line(line):
    """Parse a geomean summary line."""
    # Standard format: ./struct_impl,GEOMETRIC MEAN OF THROUGHPUTS: value
    match = re.search(r'^\./(\S+),GEOMETRIC MEAN OF THROUGHPUTS:\s*([\d.eE+-]+)', line)
    if match:
        name = match.group(1)
        gmean = float(match.group(2))
        struct, impl = parse_structure_impl(name)
        return struct, impl, gmean
    
    # Compact format: btree,mvustm,geomean=87.4675
    match = re.match(r'^(\w+),(\w+),geomean=([\d.eE+-]+)', line)
    if match:
        struct = match.group(1)
        impl = match.group(2)
        gmean = float(match.group(3))
        return struct, impl, gmean
    
    # HyTM format: geomean throughput: value (preceded by structure name)
    match = re.search(r'^geomean throughput:\s*([\d.eE+-]+)', line, re.IGNORECASE)
    if match:
        gmean = float(match.group(1))
        return None, None, gmean  # Structure needs to be determined from context
    
    return None


def parse_timing_file(filepath):
    """Parse a timing file and extract all benchmark results."""
    results = []
    geomean_data = {}
    current_section = None
    current_struct = None
    
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            
            # Track sections
            if line.startswith('=== EXPERIMENT'):
                current_section = line
                continue
            
            if line.startswith('STRUCT:'):
                current_struct = line.replace('STRUCT:', '').strip()
                continue
            
            # Track structure names (standalone lines that are structure names)
            if line and not line.startswith('./') and not line.startswith('Date:') and \
               not line.startswith('Model') and not line.startswith('Git') and \
               not line.startswith('Compiler:') and not line.startswith('OS:') and \
               not line.startswith('Hugepage:') and not line.startswith('Threads') and \
               not line.startswith('HYTM') and not line.startswith('geomean') and \
               not line.startswith('range_size') and not line.startswith('===') and \
               '=' not in line and 'range queries' not in line and ':' not in line:
                current_struct = line
                continue
            
            # Parse result lines
            result = parse_result_line(line)
            if result:
                result['section'] = current_section
                results.append(result)
                # Update current structure from result
                if result.get('structure'):
                    current_struct = result['structure']
                continue
            
            # Parse geomean lines
            gmean_result = parse_geomean_line(line)
            if gmean_result:
                struct, impl, gmean = gmean_result
                if struct is None and current_struct:
                    # HyTM format - use last known structure and impl
                    struct = current_struct
                    # Find the last impl used for this structure
                    for r in reversed(results):
                        if r['structure'] == struct:
                            impl = r['impl']
                            break
                if struct and impl:
                    geomean_data[(struct, impl)] = gmean
    
    return results, geomean_data


def plot_geomean_bars(geomean_data, output_dir, filename='geomean.pdf'):
    """
    Create normalized geomean throughput bar chart.
    Bars represent geometric mean of throughput across workloads,
    normalized to the highest throughput per structure.
    """
    # Organize data by structure
    structures = set()
    impls_found = set()
    for (struct, impl), _ in geomean_data.items():
        structures.add(struct)
        impls_found.add(impl)
    
    structures = sorted(structures, key=lambda s: STRUCT_LABELS.get(s, s))
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    # Normalize per structure
    normalized = defaultdict(dict)
    for struct in structures:
        max_val = max((geomean_data.get((struct, impl), 0) for impl in impls), default=1)
        if max_val > 0:
            for impl in impls:
                if (struct, impl) in geomean_data:
                    normalized[struct][impl] = geomean_data[(struct, impl)] / max_val
    
    # Create figure
    fig, ax = plt.subplots(figsize=(14, 6))
    
    x = np.arange(len(structures))
    n_impls = len(impls)
    width = 0.8 / n_impls
    
    for i, impl in enumerate(impls):
        values = [normalized[struct].get(impl, 0) for struct in structures]
        offset = (i - n_impls/2 + 0.5) * width
        color = IMPL_COLORS.get(impl, '#333333')
        label = IMPL_LABELS.get(impl, impl)
        ax.bar(x + offset, values, width, label=label, color=color, edgecolor='white', linewidth=0.5)
    
    # Reference line at 1.0
    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)
    
    ax.set_xlabel('Data Structure', fontsize=12)
    ax.set_ylabel('Normalized Throughput', fontsize=12)
    ax.set_title('Normalized Geomean Throughput by Data Structure\n(higher is better, normalized to max per structure)', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels([STRUCT_LABELS.get(s, s) for s in structures], rotation=45, ha='right')
    ax.legend(loc='upper right', fontsize=9, ncol=2)
    ax.grid(axis='y', alpha=0.3)
    ax.set_ylim(0, 1.15)
    
    plt.tight_layout()
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: {filename}")
    plt.close()


def aggregate_points(points):
    """
    Aggregate multiple data points with the same x-value by averaging their y-values.
    Input: list of (x, y) tuples
    Output: sorted list of (x, avg_y) tuples with unique x values
    """
    from collections import defaultdict
    x_to_ys = defaultdict(list)
    for x, y in points:
        x_to_ys[x].append(y)
    aggregated = [(x, sum(ys) / len(ys)) for x, ys in x_to_ys.items()]
    return sorted(aggregated, key=lambda p: p[0])


def plot_varying_parameter(results, param_name, param_label, output_dir, 
                           structures=None, filename_prefix='varying', quiet=False):
    """
    Create line plots showing throughput vs a varying parameter.
    One subplot per structure, lines for each implementation.
    
    For valid parameter-varying plots, we need data where ONLY that parameter
    varies while others are held at default values.
    """
    if structures is None:
        structures = list(set(r['structure'] for r in results))
    
    # Filter results by parameter
    param_values = sorted(set(r.get(param_name) for r in results if r.get(param_name) is not None))
    if not param_values:
        if not quiet:
            print(f"Warning: No data for parameter {param_name}")
        return False
    
    # Check if there's meaningful variation (more than 1 unique value)
    if len(param_values) <= 1:
        if not quiet:
            print(f"Warning: Parameter {param_name} has only {len(param_values)} unique value(s): {param_values}")
            print(f"  Skipping plot - need parameter-varying experiment data")
        return False
    
    # Organize data: data[struct][impl] = [(param_val, mops), ...]
    # For proper parameter-varying plots, we should average/aggregate results
    # that have the same param value (since other params might vary in geomean data)
    data = defaultdict(lambda: defaultdict(list))
    for r in results:
        if r.get(param_name) is not None and r['structure'] in structures:
            data[r['structure']][r['impl']].append((r[param_name], r['mops']))
    
    # Determine grid layout
    n_structs = len([s for s in structures if s in data])
    if n_structs == 0:
        print(f"Warning: No structures with data for {param_name}")
        return
    
    ncols = min(3, n_structs)
    nrows = (n_structs + ncols - 1) // ncols
    
    fig, axes = plt.subplots(nrows, ncols, figsize=(5*ncols, 4*nrows), squeeze=False)
    
    impls_found = set()
    for struct_data in data.values():
        impls_found.update(struct_data.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    struct_idx = 0
    for row in range(nrows):
        for col in range(ncols):
            ax = axes[row, col]
            if struct_idx >= len(structures):
                ax.axis('off')
                continue
            
            struct = structures[struct_idx]
            if struct not in data:
                ax.axis('off')
                struct_idx += 1
                continue
            
            for impl in impls:
                if impl not in data[struct]:
                    continue
                points = aggregate_points(data[struct][impl])
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                color = IMPL_COLORS.get(impl, '#333333')
                label = IMPL_LABELS.get(impl, impl)
                ax.plot(x_vals, y_vals, marker='o', linewidth=2, markersize=6, 
                       color=color, label=label)
            
            ax.set_xlabel(param_label, fontsize=10)
            ax.set_ylabel('Throughput (MOps/s)', fontsize=10)
            ax.set_title(STRUCT_LABELS.get(struct, struct), fontsize=11)
            ax.grid(True, alpha=0.3)
            
            # Use log scale for size parameter
            if param_name == 'n':
                ax.set_xscale('log')
            
            struct_idx += 1
    
    # Add legend to first subplot
    handles, labels = axes[0, 0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, labels, loc='upper center', ncol=max(1, len(impls)), 
                   fontsize=9, bbox_to_anchor=(0.5, 1.02))
    
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    
    filename = f'{filename_prefix}_{param_name}.pdf'
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: {filename}")
    plt.close()
    return True


def plot_varying(results, output_dir):
    """
    Five rows: Size, Thread count, Zipfian, Transaction size, Update rate
    Auto-detects which parameters have varying data and only plots those.
    """
    tree_structs = ['skiplist', 'treap', 'btree']
    binary_structs = ['list', 'avltree', 'leaftree']
    
    # First, detect which parameters have meaningful variation
    params_to_plot = []
    for param in ['n', 'p', 'z', 'trans', 'u']:
        param_values = sorted(set(r.get(param) for r in results if r.get(param) is not None))
        if len(param_values) > 1:
            params_to_plot.append(param)
    
    if not params_to_plot:
        print("Warning: No parameters with varying data found")
        return
    
    param_info = {
        'n': ('Varying Size', 'Data Structure Size (n)', 'size'),
        'p': ('Varying Thread Count', 'Number of Threads', 'threads'),
        'z': ('Varying Zipfian', 'Zipfian Parameter', 'zipf'),
        'trans': ('Varying Transaction Size', 'Operations per Transaction', 'txn'),
        'u': ('Varying Update Rate', 'Update Rate (%)', 'upd'),
    }
    
    for param in params_to_plot:
        title, label, row = param_info[param]
        print(f"Plotting: {title}")
        plot_varying_parameter(results, param, label, output_dir, 
                              structures=tree_structs, filename_prefix=f'tree_{row}', quiet=True)
        plot_varying_parameter(results, param, label, output_dir,
                              structures=binary_structs, filename_prefix=f'binary_{row}', quiet=True)


def plot_range_queries(results, output_dir, filename='range_queries.pdf'):
    """Plot range query throughput vs range size."""
    range_results = [r for r in results if r.get('rs') is not None and r.get('rqmops') is not None]
    
    if not range_results:
        print("Warning: No range query data found")
        return
    
    # Organize data
    data = defaultdict(lambda: defaultdict(list))
    for r in range_results:
        data[r['structure']][r['impl']].append((r['rs'], r['rqmops']))
    
    structures = sorted(data.keys())
    ncols = min(2, len(structures))
    nrows = (len(structures) + ncols - 1) // ncols
    
    fig, axes = plt.subplots(nrows, ncols, figsize=(6*ncols, 4*nrows), squeeze=False)
    
    impls_found = set()
    for struct_data in data.values():
        impls_found.update(struct_data.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    struct_idx = 0
    for row in range(nrows):
        for col in range(ncols):
            ax = axes[row, col]
            if struct_idx >= len(structures):
                ax.axis('off')
                continue
            
            struct = structures[struct_idx]
            
            for impl in impls:
                if impl not in data[struct]:
                    continue
                points = aggregate_points(data[struct][impl])
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                color = IMPL_COLORS.get(impl, '#333333')
                label = IMPL_LABELS.get(impl, impl)
                ax.plot(x_vals, y_vals, marker='o', linewidth=2, markersize=6,
                       color=color, label=label)
            
            ax.set_xlabel('Range Size', fontsize=10)
            ax.set_ylabel('Range Query Throughput (MOps/s)', fontsize=10)
            ax.set_title(f'{STRUCT_LABELS.get(struct, struct)} Range Queries', fontsize=11)
            ax.set_xscale('log')
            ax.set_yscale('log')
            ax.grid(True, alpha=0.3)
            ax.legend(fontsize=8)
            
            struct_idx += 1
    
    plt.tight_layout()
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: {filename}")
    plt.close()


def plot_combined_grid(results, geomean_data, output_dir, filename='results_combined.pdf'):
    """
    Multiple subplots arranged in a grid.
    """
    # Select structures to plot
    structures = ['skiplist', 'treap', 'btree', 'avltree', 'leaftree', 'list']
    available_structs = [s for s in structures if any(r['structure'] == s for r in results)]
    
    if not available_structs:
        print("Warning: No data for combined grid plot")
        return
    
    params = [
        ('n', 'Dataset Size (n)', True),   # param, label, log_scale
        ('p', 'Number of Threads', False),
        ('z', 'Zipfian Parameter', False),
        ('trans', 'Ops per Transaction', False),
        ('u', 'Update Rate (%)', False),
    ]
    
    # Filter to params that have data
    params_with_data = []
    for param, label, log in params:
        if any(r.get(param) is not None for r in results):
            # Check if there's variation in this parameter
            unique_vals = set(r.get(param) for r in results if r.get(param) is not None)
            if len(unique_vals) > 1:
                params_with_data.append((param, label, log))
    
    if not params_with_data:
        print("Warning: No varying parameters found for combined grid plot")
        return
    
    n_rows = len(params_with_data)
    n_cols = len(available_structs)
    
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(3.5*n_cols, 2.8*n_rows), squeeze=False)
    
    # Get all implementations
    impls_found = set(r['impl'] for r in results)
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    for row_idx, (param, param_label, log_scale) in enumerate(params_with_data):
        # Filter results for this parameter
        param_results = [r for r in results if r.get(param) is not None]
        
        for col_idx, struct in enumerate(available_structs):
            ax = axes[row_idx, col_idx]
            
            struct_results = [r for r in param_results if r['structure'] == struct]
            
            # Organize by implementation
            impl_data = defaultdict(list)
            for r in struct_results:
                impl_data[r['impl']].append((r[param], r['mops']))
            
            for impl in impls:
                if impl not in impl_data:
                    continue
                points = aggregate_points(impl_data[impl])
                if not points:
                    continue
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                color = IMPL_COLORS.get(impl, '#333333')
                label = IMPL_LABELS.get(impl, impl) if row_idx == 0 else None
                ax.plot(x_vals, y_vals, marker='o', linewidth=1.5, markersize=4,
                       color=color, label=label)
            
            if log_scale:
                ax.set_xscale('log')
            
            ax.grid(True, alpha=0.3, linewidth=0.5)
            
            # Labels
            if row_idx == n_rows - 1:
                ax.set_xlabel(param_label, fontsize=9)
            if col_idx == 0:
                ax.set_ylabel('Throughput\n(MOps/s)', fontsize=9)
            if row_idx == 0:
                ax.set_title(STRUCT_LABELS.get(struct, struct), fontsize=10, fontweight='bold')
            
            ax.tick_params(axis='both', labelsize=8)
    
    # Add legend at top
    handles, labels = axes[0, 0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, labels, loc='upper center', ncol=min(len(impls), 6), fontsize=9, 
                   bbox_to_anchor=(0.5, 1.02))
    
    plt.tight_layout(rect=[0, 0, 1, 0.96])
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: {filename}")
    plt.close()


def main():
    parser = argparse.ArgumentParser(description='Plot paper experiments')
    parser.add_argument('input_path', type=str, 
                       help='Path to timing file or directory containing timing files')
    parser.add_argument('-o', '--output-dir', type=str, default='plots',
                       help='Output directory for plots (default: plots)')
    parser.add_argument('--plots', nargs='+', 
                       choices=['geomean', 'varying', 'range', 'combined', 'all'],
                       default=['all'],
                       help='Which plots to generate (default: all)')
    parser.add_argument('--format', type=str, choices=['pdf', 'png', 'svg'], 
                       default='pdf', help='Output format (default: pdf)')
    
    args = parser.parse_args()
    
    input_path = Path(args.input_path)
    if not input_path.exists():
        print(f"Error: Path '{args.input_path}' not found")
        return 1
    
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)
    
    # Set matplotlib backend for file output
    plt.switch_backend('Agg')
    
    # Determine if input is a directory or file
    if input_path.is_dir():
        # New directory-based format
        print(f"Processing directory: {input_path}")
        
        exps = [
            'geomean',
            'vary_size',
            'vary_threads',
            'vary_zipfian',
            'vary_trans',
            'vary_update',
            'range_queries'
        ]
        
        all_results = []
        all_geomean_data = {}
        
        for filename in exps.items():
            filepath = input_path / filename
            if filepath.exists():
                print(f"  Parsing {filename}...")
                results, geomean_data = parse_timing_file(filepath)
                all_results.extend(results)
                all_geomean_data.update(geomean_data)
                print(f"    Found {len(results)} results, {len(geomean_data)} geomeans")
            else:
                print(f"  Skipping {filename} (not found)")
        
        results = all_results
        geomean_data = all_geomean_data
    else:
        # Single file (legacy format)
        print(f"Parsing {input_path}...")
        results, geomean_data = parse_timing_file(input_path)
    
    print(f"\nTotal: {len(results)} benchmark results, {len(geomean_data)} geometric mean summaries")
    
    if not results and not geomean_data:
        print("Error: No data found")
        return 1
    
    plots = args.plots
    if 'all' in plots:
        plots = ['geomean', 'varying', 'range', 'combined']
    
    if 'geomean' in plots and geomean_data:
        print("\nGenerating Geomean bar chart...")
        plot_geomean_bars(geomean_data, output_dir, 
                                  f'geomean.{args.format}')
    
    if 'varying' in plots and results:
        print("\nGenerating Parameter variation...")
        print("Note: These require parameter-varying experiment data.")
        print("      If you only have geomean data, these plots will be skipped.")
        plot_varying(results, output_dir)
    
    if 'range' in plots and results:
        print("\nGenerating Range Query plot...")
        plot_range_queries(results, output_dir, f'range_queries.{args.format}')
    
    if 'combined' in plots and results:
        print("\nGenerating Combined grid plot...")
        plot_combined_grid(results, geomean_data, output_dir, 
                          f'results_combined.{args.format}')
    
    print(f"\nAll plots saved to {output_dir}/")
    return 0


if __name__ == '__main__':
    sys.exit(main())
