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
from matplotlib.patches import Patch
import matplotlib.patches as patches
import numpy as np
from itertools import product


pdfmerge = True
try:
    import fitz
except ImportError:
   print("ERROR: import fitz failed, not merging pdfs (pip install pymupdf)\n")
   pdfmerge = False

# Map impl -> color
IMPL_COLORS = {
    'mvustm': '#1f77b4',
    'mvustm_indirect': '#1f77b4',
    'mvustm_hwstamp': '#2ca02c',
    'mvustm_lzstamp': '#ff7f0e',
    'mvustm_eager': '#9467bd',
    'mvustm_agg_funnel_eager': '#8c564b',
    'ustm': '#d62728',
    'ustm_longjmp': '#e377c2',
    'ustm_simple_longjmp': '#17becf',
    'ustm_longjmp_hwstamp': '#aec7e8',
    'mvustm_longjmp': '#9467bd',
    'mvustm_trythrow': '#8c564b',
    'mvustm_unprotected': '#17becf',
    '2plsf': '#7f7f7f',
    'hytm': '#bcbd22',
    'tiny': '#ff9896',
    'fuse': '#e41a1c',
    'fuse_hwstamp': '#ff6347',
    'multiverse': '#333333',
}

IMPL_LABELS = {
    'mvustm': '$\\mu$STM',
    'mvustm_indirect': '$\\mu$STM-Indirect',
    'mvustm_hwstamp': '$\\mu$STM-HWStamp',
    'mvustm_lzstamp': '$\\mu$STM-LZStamp',
    'mvustm_eager': '$\\mu$STM-Eager',
    'mvustm_agg_funnel_eager': '$\\mu$STM-AggFunnel',
    'ustm': 'SV-$\\mu$STM',
    'ustm_longjmp': '$\\mu$STM-Longjmp',
    'ustm_simple_longjmp': 'SV-$\\mu$STM-Longjmp',
    'ustm_longjmp_hwstamp': '$\\mu$STM-Longjmp-HWStamp',
    'mvustm_longjmp': '$\\mu$STM-Longjmp',
    'mvustm_trythrow': '$\\mu$STM-TryThrow',
    'mvustm_unprotected': '$\\mu$STM-Unprotected',
    '2plsf': '2PLSF',
    'hytm': 'HyTM',
    'tiny': 'TinySTM',
    'fuse': 'Fuse',
    'fuse_hwstamp': 'Fuse-HWStamp',
    'multiverse': 'Multiverse',
}

# Map impl -> marker shape (unique per implementation for easy identification)
IMPL_MARKERS = {
    'mvustm': 's',
    'mvustm_indirect': 'o',
    'mvustm_hwstamp': 'P',
    'mvustm_lzstamp': 'h',
    'mvustm_eager': 'H',
    'mvustm_agg_funnel_eager': 'd',
    'ustm': 'p',
    'ustm_longjmp': 'v',
    'ustm_simple_longjmp': '<',
    'ustm_longjmp_hwstamp': '>',
    'mvustm_longjmp': '8',
    'mvustm_trythrow': 'h',
    'mvustm_unprotected': 'H',
    '2plsf': '^',
    'hytm': 'd',
    'tiny': 'v',
    'fuse': 'v',
    'fuse_hwstamp': '<',
    'multiverse': 'X',
}

# Map impl -> linestyle (solid for inplace, dashed for non-inplace when pair exists)
IMPL_LINESTYLES = {
    'mvustm': '--',
    'mvustm_indirect': ':',
    'mvustm_hwstamp': '-',
    'mvustm_lzstamp': '-.',
    'mvustm_eager': ':',
    'mvustm_agg_funnel_eager': '-',
    'ustm': '-',
    'ustm_longjmp': '--',
    'ustm_simple_longjmp': '-.',
    'ustm_longjmp_hwstamp': ':',
    'mvustm_longjmp': '--',
    'mvustm_trythrow': '-.',
    'mvustm_unprotected': ':',
    '2plsf': '--',
    'hytm': '-',
    'tiny': '-',
    'fuse': '--',
    'fuse_hwstamp': '-.',
    'multiverse': '--',
}

STRUCT_LABELS = {
    'btree': 'B-tree',
    'btree_inplace': 'B-tree Inplace',
    'arttree': 'AR Tree',
    'avltree': 'AVL Tree',
    'leaftree': 'Leaf Tree',
    'treap': 'Treap',
    'skiplist': 'Skip List',
    'list': 'Linked List',
    'hash_block': 'Hash Table',
    'hash_list': 'Hash List',
    'hash_probe': 'Hash Probe',
    'mix': 'Mixed',
}

STRUCT_HATCHES = {
    'btree': '/',
    'btree_inplace': '//',
    'arttree': '\\',
    'avltree': '|',
    'leaftree': '\\/',
    'treap': '-',
    'skiplist': 'x',
    'list': '.',
    'hash_block': '+',
    'hash_list': '++',
    'hash_probe': '+++',
    'mix': '+/|',
}

# Implementation ordering (for consistent legend order)
IMPL_ORDER = [
    'mvustm', 'mvustm_hwstamp', 'mvustm_lzstamp', 'mvustm_eager',
    'mvustm_agg_funnel_eager',
    'ustm', 'ustm_longjmp', 'ustm_simple_longjmp',
    'mvustm_longjmp', 'mvustm_trythrow',
    'mvustm_unprotected', '2plsf', 'hytm', 'tiny',
    'fuse', 'fuse_hwstamp', 'multiverse', 'dcotl',
]

# Known implementations (longer matches first)
KNOWN_IMPLS = [
    'mvustm_agg_funnel_eager', 'ustm_simple_longjmp',
    'ustm_longjmp_hwstamp', 'mvustm_lzstamp', 'mvustm_hwstamp',
    'mvustm_longjmp', 'mvustm_trythrow', 'mvustm_unprotected',
    'mvustm_indirect', 'mvustm_eager', 'fuse_hwstamp',
    'ustm_longjmp', 'mvustm', 'ustm',
    '2plsf', 'hytm', 'tiny', 'fuse', 'multiverse',
]

#TODO: should be made into a config class
fontsize = 11.5
paper_plot_structures = ['avltree', 'btree', 'hash_list']

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
                'u': r'u=(-?\d+)',
                'upsert': r'upsert=(\d+)',
                'trans': r'trans=(\d+)',
                'n': r'n=(\d+)',
                'p': r'p=(\d+)',
                'z': r'z=([\d.]+)',
                'pa': r'pa=([\d.]+)',
                'rqmops': r'rqmops=([\d.eE+-]+)',
                'mops': r'(?<!rq)mops=([\d.eE+-]+)',
                'rs': r'rs=(\d+)',
                'rqp': r'rqp=(\d+)',
            }
            
            for param, pattern in param_patterns.items():
                pmatch = re.search(pattern, params_str)
                if pmatch:
                    val = pmatch.group(1)
                    if param in ['u', 'upsert', 'trans', 'n', 'p', 'rs', 'rqp']:
                        result[param] = int(val)
                    else:
                        result[param] = float(val)
            
            # Treat 'upsert' as 'u' for plotting purposes
            if 'upsert' in result and 'u' not in result:
                result['u'] = result.pop('upsert')
            elif 'upsert' in result:
                result.pop('upsert')
            
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
        'u': r'u=(-?\d+)',
        'upsert': r'upsert=(\d+)',
        'trans': r'trans=(\d+)',
        'n': r'n=(\d+)',
        'p': r'p=(\d+)',
        'z': r'z=([\d.]+)',
        'pa': r'pa=([\d.]+)',
        'rqmops': r'rqmops=([\d.eE+-]+)',
        'mops': r'(?<!rq)mops=([\d.eE+-]+)',
        'rs': r'rs=(\d+)',
        'rqp': r'rqp=(\d+)',
    }
    
    for param, pattern in param_patterns.items():
        pmatch = re.search(pattern, params_str)
        if pmatch:
            val = pmatch.group(1)
            if param in ['u', 'upsert', 'trans', 'n', 'p', 'rs', 'rqp']:
                result[param] = int(val)
            else:
                result[param] = float(val)
    
    # Treat 'upsert' as 'u' for plotting purposes
    if 'upsert' in result and 'u' not in result:
        result['u'] = result.pop('upsert')
    elif 'upsert' in result:
        result.pop('upsert')
    
    return result if 'mops' in result else None


def parse_geomean_line(line):
    """Parse a geomean summary line."""
    # Duplicated prefix format: ./struct_impl,./struct_impl,GEOMETRIC MEAN OF THROUGHPUTS: value
    match = re.search(r'^\./([^,]+),\./[^,]+,GEOMETRIC MEAN OF THROUGHPUTS:\s*([\d.eE+-]+)', line)
    if match:
        name = match.group(1)
        gmean = float(match.group(2))
        struct, impl = parse_structure_impl(name)
        return struct, impl, gmean
    
    # Standard format: ./struct_impl,GEOMETRIC MEAN OF THROUGHPUTS: value
    match = re.search(r'^\./([^,]+),GEOMETRIC MEAN OF THROUGHPUTS:\s*([\d.eE+-]+)', line)
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

    #combos = sorted({(d['structure'], d['impl']) for d in results})
    #for c in combos:
    #    if c not in geomean_data:
    #            struct, impl = c
    #            matching = [d for d in results if d['structure'] == struct and d['impl'] == impl]
    #            print("MATCHED")
    #            for m in matching: print(m)
    
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
    fig, ax = plt.subplots(figsize=(6, 2.5))
    
    x = np.arange(len(structures))
    n_impls = len(impls)
    width = 0.6 / n_impls

    inner_spacing = 1.5
    
    bars = []
    for i, impl in enumerate(impls):
        values = [normalized[struct].get(impl, 0) for struct in structures]
        values_notnormalized = [geomean_data.get((struct, impl)) for struct in structures]
        offset = (i - n_impls/2 + 0.5) * width * inner_spacing
        color = IMPL_COLORS.get(impl, '#333333')
        label = IMPL_LABELS.get(impl, impl)
        bars.append((values, values_notnormalized,
                     ax.bar(x + offset,
                           values,
                           width,
                           label=label,
                           color=color,
                           edgecolor='black',
                           linewidth=1)))

    for values, values_notnormalized, bar in bars:
        for b,v,vnn in zip(bar, values, values_notnormalized):
            ax.text(
                b.get_x() + b.get_width() / 2,
                b.get_height(),              # use normalized height for position
                f'{vnn:.1f}',              # use RAW value for label
                ha='center',
                va='bottom',
                fontsize=fontsize-3,
            )
    
    # Reference line at 1.0
    ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.5)
    
    ax.set_xlabel('Data Structure', fontsize=fontsize-2)
    ax.set_ylabel('Normalized Throughput', fontsize=fontsize-2)
    #ax.set_title('Normalized Geomean Throughput by Data Structure\n(higher is better, normalized to max per structure)', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels([STRUCT_LABELS.get(s, s) for s in structures], rotation=0, ha='center', fontsize=fontsize-3)
    ax.tick_params(axis='y', labelsize=fontsize-3)
    ax.legend(loc='upper center', fontsize=fontsize-2.5, ncol=n_impls, bbox_to_anchor=(0.5, 1.25))
    ax.grid(axis='y', alpha=0.3)
    ax.set_ylim(0, 1.1)
    
    plt.tight_layout()
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight', pad_inches=0.015)
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
    Create individual line plots showing throughput vs a varying parameter.
    One plot per structure, lines for each implementation.
    
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
    data = defaultdict(lambda: defaultdict(list))
    for r in results:
        if r.get(param_name) is not None and r['structure'] in structures:
            data[r['structure']][r['impl']].append((r[param_name], r['mops']))
    
    # Check if we have data
    n_structs = len([s for s in structures if s in data])
    if n_structs == 0:
        print(f"Warning: No structures with data for {param_name}")
        return False
    
    impls_found = set()
    for struct_data in data.values():
        impls_found.update(struct_data.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    # Generate individual plot for each structure
    for struct in structures:
        if struct not in data:
            continue
        
        fig, ax = plt.subplots(figsize=(8/2.3, 6/2.3))
        
        for impl in impls:
            if impl not in data[struct]:
                continue
            points = aggregate_points(data[struct][impl])
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7, 
                   color=color, label=label, linestyle=ls)

        ax.set_xticks(x_vals)
        ax.set_xlabel(param_label, fontsize=fontsize)
        ax.tick_params(axis='x', labelsize=fontsize-1)
        if struct == paper_plot_structures[0]:
            ax.set_ylabel('Throughput (MOps/s)', fontsize=fontsize)
        ax.set_title(f'{STRUCT_LABELS.get(struct, struct)}', fontsize=fontsize+1, fontweight='bold')
        ax.tick_params(axis='y', labelsize=fontsize-1)
        ax.grid(True, alpha=0.3)
        #ax.legend(loc='best', fontsize=fontsize+2)
        
        # Use log scale for size parameter
        if param_name == 'n':
            ax.set_xscale('log')
        
        plt.tight_layout()
        
        filename = f'{filename_prefix}_{struct}.pdf'
        plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight', pad_inches=0.015)
        print(f"Generated: {filename}")
        plt.close()
    
    return True


def plot_varying(results, output_dir, param):
    """
    Plot throughput vs a single varying parameter.
    Generates one plot per structure for both tree and other structure groups.
    """
    tree_structs = ['skiplist', 'treap', 'btree', 'arttree']
    other_structs = ['list', 'avltree', 'leaftree', 'hash_block', 'hash_list']
    
    param_values = sorted(set(r.get(param) for r in results if r.get(param) is not None))
    if len(param_values) <= 1:
        return
    
    param_info = {
        'n': ('Varying Size', 'Data Structure Size (n)', 'size'),
        'p': ('Varying Thread Count', 'Number of Threads', 'threads'),
        'z': ('Varying Zipfian', 'Zipfian Parameter', 'zipfian'),
        'trans': ('Varying Transaction Size', 'Operations per Transaction', 'trans'),
        'u': ('Varying Update Rate', 'Update Rate (%)', 'update'),
    }
    
    title, label, shortname = param_info[param]
    print(f"Plotting: {title}")
    plot_varying_parameter(results, param, label, output_dir, 
                          structures=tree_structs, filename_prefix=shortname, quiet=True)
    plot_varying_parameter(results, param, label, output_dir,
                          structures=other_structs, filename_prefix=shortname, quiet=True)


def plot_range_queries(results, output_dir):
    """Plot individual range query throughput vs range size per structure."""
    range_results = [r for r in results if r.get('rs') is not None and r.get('rqmops') is not None]
    
    if not range_results:
        print("Warning: No range query data found")
        return
    
    # Organize data for both metrics
    rq_data = defaultdict(lambda: defaultdict(list))  # range query throughput
    normal_data = defaultdict(lambda: defaultdict(list))  # normal throughput
    for r in range_results:
        struct = r['structure']
        impl = r['impl']
        rs = r['rs']
        if r.get('rqmops') is not None:
            rq_data[struct][impl].append((rs, r['rqmops']))
        if r.get('mops') is not None:
            normal_data[struct][impl].append((rs, r['mops']))
    
    structures = sorted(rq_data.keys())
    
    impls_found = set()
    for struct_data in rq_data.values():
        impls_found.update(struct_data.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    # Generate individual plot for each structure (range query throughput)
    for struct in structures:
        fig, ax = plt.subplots(figsize=(8, 6))
        
        for impl in impls:
            if impl not in rq_data[struct]:
                continue
            points = aggregate_points(rq_data[struct][impl])
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                   color=color, label=label, linestyle=ls)
        
        ax.set_xlabel('Range Size', fontsize=12)
        ax.set_ylabel('Range Query Throughput (MOps/s)', fontsize=12)
        ax.set_title(f'{STRUCT_LABELS.get(struct, struct)} - Range Queries', fontsize=14)
        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.grid(True, alpha=0.3)
        ax.legend(loc='best', fontsize=10)
        
        plt.tight_layout()
        
        filename = f'{struct}_range.pdf'
        plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
        print(f"Generated: {filename}")
        plt.close()
    
    # Generate individual plot for each structure (normal throughput)
    for struct in structures:
        if struct not in normal_data:
            continue
            
        fig, ax = plt.subplots(figsize=(8, 6))
        
        for impl in impls:
            if impl not in normal_data[struct]:
                continue
            points = aggregate_points(normal_data[struct][impl])
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                   color=color, label=label, linestyle=ls)
        
        ax.set_xlabel('Range Size', fontsize=12)
        ax.set_ylabel('Update Throughput (MOps/s)', fontsize=12)
        ax.set_title(f'{STRUCT_LABELS.get(struct, struct)} - Update Throughput', fontsize=14)
        ax.set_xscale('log')
        ax.set_yscale('log')
        ax.grid(True, alpha=0.3)
        ax.legend(loc='best', fontsize=10)
        
        plt.tight_layout()
        
        filename = f'{struct}_range_normal.pdf'
        plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
        print(f"Generated: {filename}")
        plt.close()
    
    # Generate combined figure for btree with both metrics
    if 'btree' in structures:
        fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(7, 2.5))
        
        # Left plot: Range query throughput
        for impl in impls:
            if impl not in rq_data['btree']:
                continue
            points = aggregate_points(rq_data['btree'][impl])
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax1.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                    color=color, label=label, linestyle=ls)
        
        ax1.set_xlabel('Range Size', fontsize=10)
        ax1.set_ylabel('Range Query Throughput (MOps/s)', fontsize=10)
        ax1.set_title('B-tree - Range Query Throughput', fontsize=11.5)
        ax1.set_xscale('log')
        ax1.set_yscale('log')
        ax1.grid(True, alpha=0.3)
        #ax1.legend(loc='best', fontsize=10)

        ax1.set_ylim(bottom=0)
        
        # Right plot: Normal throughput
        for impl in impls:
            if impl not in normal_data['btree']:
                continue
            points = aggregate_points(normal_data['btree'][impl])
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax2.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                    color=color, linestyle=ls)
        
        ax2.set_xlabel('Range Size', fontsize=10)
        ax2.set_ylabel('Update Throughput (MOps/s)', fontsize=10)
        ax2.set_title('B-tree - Update Throughput', fontsize=11.5)
        ax2.set_xscale('log')
        ax2.set_yscale('log')
        ax2.grid(True, alpha=0.3)
        #ax2.legend(loc='best', fontsize=10)

        ax2.set_ylim(0, ax1.get_ylim()[1])
        
        plt.tight_layout()

        fig.legend(loc='upper center', ncol=len(impls), fontsize=10, bbox_to_anchor=(0.5, 0.07))
        
        filename = 'btree_range_combined.pdf'
        plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight', pad_inches=0.005)
        print(f"Generated: {filename}")
        plt.close()


def plot_paper_row_(results, output_dir, param, structures, name='plot', fmt='pdf'):
    """
    Generate a compact 1x3 subplot figure suitable for a two-column paper.
    Matches the style of typical systems-conference plots.
    """
    param_info = {
        'n': ('Size (n)', True),
        'p': ('Number of Threads', False),
        'z': ('Zipfian Parameter', False),
        'trans': ('Ops per Transaction', False),
        'u': ('Update Rate (%)', False),
    }
    if param not in param_info:
        print(f"Warning: Unknown parameter '{param}'")
        return
    param_label, log_scale = param_info[param]

    # Organize data
    data = defaultdict(lambda: defaultdict(list))
    for r in results:
        if r.get(param) is not None and r['structure'] in structures:
            data[r['structure']][r['impl']].append((r[param], r['mops']))

    available = [s for s in structures if s in data]
    if not available:
        print(f"Warning: No data for paper row ({param})")
        return

    impls_found = set()
    for sd in data.values():
        impls_found.update(sd.keys())
    impls = [i for i in IMPL_ORDER if i in impls_found]

    n = len(available)
    fig, axes = plt.subplots(1, n, figsize=(2.4 * n, 2.0), squeeze=False)
    axes = axes[0]

    for col, struct in enumerate(available):
        ax = axes[col]
        for impl in impls:
            if impl not in data[struct]:
                continue
            points = aggregate_points(data[struct][impl])
            if not points:
                continue
            x_vals = [p[0] for p in points]
            y_vals = [p[1] for p in points]
            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            mkr = IMPL_MARKERS.get(impl, 'o')
            ls = IMPL_LINESTYLES.get(impl, '-')
            ax.plot(x_vals, y_vals, marker=mkr, linewidth=1.2, markersize=4,
                    color=color, label=label, linestyle=ls)

        if log_scale:
            ax.set_xscale('log')
        ax.set_xlabel(param_label, fontsize=7)
        if col == 0:
            ax.set_ylabel('Throughput\n(MOps/s)', fontsize=7)
        ax.set_title(STRUCT_LABELS.get(struct, struct), fontsize=8, fontweight='bold')
        ax.tick_params(axis='both', labelsize=6)
        ax.grid(True, alpha=0.3, linewidth=0.4)

    # Single shared legend across the top
    handles, labels = axes[0].get_legend_handles_labels()
    if handles:
        fig.legend(handles, labels, loc='upper center',
                   ncol=min(len(impls), 6), fontsize=6,
                   bbox_to_anchor=(0.5, 1.12), columnspacing=1.0,
                   handlelength=2.0)

    plt.tight_layout(rect=[0, 0, 1, 0.95])
    filename = f'{name}.{fmt}'
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: paper/{filename}")
    plt.close()

def plot_paper_row(results, output_dir, input_dir, param, structures, name='plot', fmt='pdf'):
    to_merge = []
    for struct in structures:
        pdf = f"{input_dir}/{name}_{struct}.{fmt}"
        to_merge.append(pdf)
    filename = f'{output_dir}/{name}.{fmt}'
    merge_pdfs_horizontally(to_merge, filename)

def merge_pdfs_horizontally(pdf_list, output_path):
    # Open all documents
    docs = [fitz.open(pdf) for pdf in pdf_list]
    # Grab the first page of each document
    # (Note: If your PDFs have multiple pages, see the note below)
    pages = [doc[0] for doc in docs]
    # Calculate the new page dimensions
    # Total width is the sum of all page widths
    total_width = sum(page.rect.width for page in pages)
    # Total height is the maximum height among all pages
    max_height = max(page.rect.height for page in pages)
    # Create a new blank document and page
    out_doc = fitz.open()
    out_page = out_doc.new_page(width=total_width, height=max_height)
    # Paste each page side-by-side
    current_x = 0
    for i, page in enumerate(pages):
        # Define the rectangle where the current page will be placed
        # Rect(x0, y0, x1, y1)
        rect = fitz.Rect(current_x, 0, current_x + page.rect.width, page.rect.height)
        # Paste the page from the original document
        out_page.show_pdf_page(rect, docs[i], 0)
        # Update the X-coordinate for the next page to start right where this one ends
        current_x += page.rect.width
    # Save the result
    out_doc.save(output_path)
    # Clean up and close all documents
    for doc in docs:
        doc.close()
    out_doc.close()
    print(f"merged {len(pdf_list)} pdfs to {output_path}")


def plot_paper_figures(exp_results, output_dir, fmt='pdf'):
    """
    Generate compact paper figures for ART, B-tree, Skip List.
    Thread scalability, update rate, zipfian, ops/txn, and size.
    """
    paper_dir = output_dir / 'paper'
    paper_dir.mkdir(exist_ok=True)
    file_to_param = {
        'vary_threads': ('p', 'threads'),
        'vary_update': ('u', 'update'),
        'vary_zipfian': ('z', 'zipfian'),
        'vary_trans': ('trans', 'trans'),
        'vary_size': ('n', 'size'),
    }
    for exp_file, (param, name) in file_to_param.items():
        if exp_file in exp_results and exp_results[exp_file]:
            #plot_paper_row(exp_results[exp_file], paper_dir, param,
            #               paper_plot_structures, name=name, fmt=fmt)
            plot_paper_row(exp_results[exp_file], paper_dir, output_dir, param,
                           paper_plot_structures, name=name, fmt=fmt)


def plot_combined_grid(exp_results, geomean_data, output_dir, filename='results_combined.pdf'):
    """
    Multiple subplots arranged in a grid.
    Uses per-experiment-file results so each row only plots data from the
    experiment where that parameter was actually varied.
    """
    # Map each parameter to its corresponding experiment file
    param_to_file = {
        'n': 'vary_size',
        'p': 'vary_threads',
        'z': 'vary_zipfian',
        'trans': 'vary_trans',
        'u': 'vary_update',
    }

    # Select structures to plot
    structures = ['btree', 'arttree', 'skiplist', 'treap', 'avltree', 'leaftree', 'hash_block', 'hash_list', 'list']
    all_results = []
    for key in param_to_file.values():
        if key in exp_results:
            all_results.extend(exp_results[key])
    available_structs = [s for s in structures if any(r['structure'] == s for r in all_results)]
    
    if not available_structs:
        print("Warning: No data for combined grid plot")
        return
    
    params = [
        ('n', 'Size (n)', True),   # param, label, log_scale
        ('p', 'Number of Threads', False),
        ('z', 'Zipfian Parameter', False),
        ('trans', 'Ops per Transaction', False),
        ('u', 'Update Rate (%)', False),
    ]
    
    # Filter to params that have data in their corresponding experiment file
    params_with_data = []
    for param, label, log in params:
        exp_file = param_to_file[param]
        file_results = exp_results.get(exp_file, [])
        if file_results:
            unique_vals = set(r.get(param) for r in file_results if r.get(param) is not None)
            if len(unique_vals) > 1:
                params_with_data.append((param, label, log))
    
    if not params_with_data:
        print("Warning: No varying parameters found for combined grid plot")
        return
    
    n_rows = len(params_with_data)
    n_cols = len(available_structs)
    
    fig, axes = plt.subplots(n_rows, n_cols, figsize=(3.5*n_cols, 2.8*n_rows), squeeze=False)
    
    # Get all implementations across all experiment files
    impls_found = set(r['impl'] for r in all_results)
    impls = [i for i in IMPL_ORDER if i in impls_found]
    
    for row_idx, (param, param_label, log_scale) in enumerate(params_with_data):
        # Use only results from the correct experiment file for this parameter
        exp_file = param_to_file[param]
        param_results = [r for r in exp_results.get(exp_file, []) if r.get(param) is not None]
        
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
                mkr = IMPL_MARKERS.get(impl, 'o')
                ls = IMPL_LINESTYLES.get(impl, '-')
                ax.plot(x_vals, y_vals, marker=mkr, linewidth=1.5, markersize=5,
                       color=color, label=label, linestyle=ls)
            
            if log_scale:
                ax.set_xscale('log')
            
            ax.grid(True, alpha=0.3, linewidth=0.5)
            
            # Labels — every row gets its x-axis label since each row is a different parameter
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


def parse_tpcc_file(filepath):
    """Parse a TPCC timing file. Returns list of {struct, impl, mops, aborts} dicts."""
    results = []
    with open(filepath, 'r') as f:
        for line in f:
            line = line.strip()
            if line.startswith('#') or not line.startswith('./'):
                continue
            # Format: ./tpcc_<struct>_<impl>: mops/sec = <float>, num_warehouses = <int>, %aborts = <float>
            # Also handle: ./tpcc_<struct>_<impl>: mops/sec = <float>, num_ warehouses = <int>, %aborts = <float>
            match = re.match(
                r'^\./tpcc_(\w+)_([\w]+):\s*mops/sec\s*=\s*([\d.eE+-]+),\s*num_?\s*warehouses\s*=\s*(\d+),\s*%aborts\s*=\s*([\d.eE+-]+)',
                line)
            if match:
                struct = match.group(1)
                impl = match.group(2)
                mops = float(match.group(3))
                aborts = float(match.group(5))
                results.append({
                    'structure': struct,
                    'impl': impl,
                    'mops': mops,
                    'aborts': aborts,
                })
    return results


def plot_tpcc_bars(tpcc_results, output_dir, filename='tpcc.pdf'):
    """
    Create TPCC throughput bar chart.
    Groups by structure (hash_block, btree, mix), bars for each implementation.
    Averages multiple trials per (struct, impl).
    """
    if not tpcc_results:
        print("Warning: No TPCC data found")
        return

    # Average trials per (struct, impl)
    from collections import defaultdict
    trial_data = defaultdict(list)
    for r in tpcc_results:
        trial_data[(r['structure'], r['impl'])].append(r['mops'])

    tpcc_structs = ['hash_block', 'btree', 'mix']
    #structures = [s for s in tpcc_structs if any(k[0] == s for k in trial_data)]
    structures = list({item['structure'] for item in tpcc_results})
    
    impls_found = set(k[1] for k in trial_data)
    tpcc_impl_order = ['mvustm', 'fuse', '2plsf', 'multiverse', 'tiny']
    impls = [i for i in tpcc_impl_order if i in impls_found]

    fig, ax = plt.subplots(figsize=(4, 2.6))
    x = np.arange(len(structures))
    n_impls = len(impls)
    width = 0.6 / max(n_impls, 1)

    inner_spacing = 1.5

    for i, impl in enumerate(impls):
        values = []
        for struct in structures:
            trials = trial_data.get((struct, impl), [])
            values.append(sum(trials) / len(trials) if trials else 0)
        offset = (i - n_impls / 2 + 0.5) * width * inner_spacing
        color = IMPL_COLORS.get(impl, '#333333')
        label = IMPL_LABELS.get(impl, impl)
        bars = ax.bar(x + offset, values, width, label=label, color=color,
                      edgecolor='black', linewidth=1)
        # Add value labels on bars
        for bar, val in zip(bars, values):
            if val > 0:
                ax.text(bar.get_x() + bar.get_width() / 2, bar.get_height() + 0.05,
                        f'{val:.1f}', ha='center', va='bottom', fontsize=8)

    ax.set_ylim(0, 1.3 * ax.get_ylim()[1])
    ax.set_xlabel('Data Structure', fontsize=12)
    ax.set_ylabel('Throughput (MOps/s)', fontsize=12)
    #ax.set_title('TPC-C Throughput', fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels([STRUCT_LABELS.get(s, s) for s in structures])
    ax.legend(loc='upper right', fontsize=10, ncols=n_impls)
    ax.grid(axis='y', alpha=0.3)


    plt.tight_layout()
    plt.savefig(output_dir / filename, dpi=300, bbox_inches='tight')
    print(f"Generated: {filename}")
    plt.close()


def plot_nontrivial_types(results, output_dir, fmt='pdf'):
    """
    Plot non-trivial types experiments.
    Part 1: btree inplace vs indirect (varying update rate) — single plot
    Part 2: hash_probe (and hash_list if present) across impls (varying update rate)
    """
    if not results:
        print("Warning: No nontrivial types data found")
        return

    # Organize data
    data = defaultdict(lambda: defaultdict(list))
    for r in results:
        key = f"{r['structure']}_{r['impl']}"
        data[r['structure']][r['impl']].append((r.get('u', 0), r['mops']))

    # --- Part 1: btree inplace vs indirect ---
    inplace_data = data.get('btree_inplace', {})
    indirect_data = data.get('btree', {})
    if inplace_data or indirect_data:
        # Plot order: for each impl, show inplace then indirect
        btree_impl_order = ['mvustm', '2plsf', 'fuse', 'multiverse']
        all_impls = set(inplace_data.keys()) | set(indirect_data.keys())
        ordered_impls = [i for i in btree_impl_order if i in all_impls]
        ordered_impls += [i for i in all_impls if i not in ordered_impls]

        fig, ax = plt.subplots(figsize=(9, 6))
        for impl in ordered_impls:
            color = IMPL_COLORS.get(impl, '#333333')
            mkr = IMPL_MARKERS.get(impl, 'o')
            base_label = IMPL_LABELS.get(impl, impl)
            # Inplace variant (solid line)
            if impl in inplace_data:
                points = aggregate_points(inplace_data[impl])
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                        color=color, label=f'{base_label} (inplace)', linestyle='-')
            # Indirect variant (dashed line)
            if impl in indirect_data:
                points = aggregate_points(indirect_data[impl])
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                        color=color, label=f'{base_label} (indirect)', linestyle='--')

        ax.set_xlabel('Update Rate (%)', fontsize=12)
        ax.set_ylabel('Throughput (MOps/s)', fontsize=12)
        ax.set_title('B-tree: Inplace vs Indirect Upserts', fontsize=14)
        ax.grid(True, alpha=0.3)
        ax.legend(loc='best', fontsize=10)
        plt.tight_layout()
        plt.savefig(output_dir / f'nontrivial_btree.{fmt}', dpi=300, bbox_inches='tight')
        print(f"Generated: nontrivial_btree.{fmt}")
        plt.close()

    # --- Part 2: hash_probe and hash_list together ---
    hash_structs = [s for s in ['hash_probe', 'hash_list'] if s in data]
    if hash_structs:
        target_impls = ['mvustm', 'mvustm_indirect', 'multiverse', 'fuse', '2plsf']
        fig, ax = plt.subplots(figsize=(9, 6))
        has_lines = False
        for struct in hash_structs:
            impls_found = set(data[struct].keys())
            impls = [i for i in target_impls if i in impls_found]
            struct_label = STRUCT_LABELS.get(struct, struct)
            for impl in impls:
                points = aggregate_points(data[struct][impl])
                x_vals = [p[0] for p in points]
                y_vals = [p[1] for p in points]
                color = IMPL_COLORS.get(impl, '#333333')
                impl_label = IMPL_LABELS.get(impl, impl)
                mkr = IMPL_MARKERS.get(impl, 'o')
                ls = IMPL_LINESTYLES.get(impl, '-')
                label = f'{struct_label} {impl_label}'
                ax.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                        color=color, label=label, linestyle=ls)
                has_lines = True

        if has_lines:
            ax.set_xlabel('Update Rate (%)', fontsize=12)
            ax.set_ylabel('Throughput (MOps/s)', fontsize=12)
            ax.set_title('Hash Tables — Varying Update Rate', fontsize=14)
            ax.grid(True, alpha=0.3)
            ax.legend(loc='best', fontsize=10)
            plt.tight_layout()
            plt.savefig(output_dir / f'nontrivial_hash.{fmt}', dpi=300, bbox_inches='tight')
            print(f"Generated: nontrivial_hash.{fmt}")
            plt.close()


def plot_ablation(results, output_dir, fmt='pdf'):
    """
    Plot ablation studies on btree as a single side-by-side figure.
    Left: Timestamping variants (split/default, hwstamp, lzstamp, eager, agg_funnel_eager, fuse, fuse_hwstamp)
    Right: Abort mechanism variants (ustm, ustm_longjmp, ustm_simple_longjmp)
    """
    if not results:
        print("Warning: No ablation data found")
        return

    data = defaultdict(list)
    for r in results:
        data[r['impl']].append((r.get('u', 0), r['mops']))

    timestamp_order = [
        'mvustm', 'mvustm_hwstamp', 'mvustm_lzstamp',
        'mvustm_eager', 'mvustm_agg_funnel_eager', 'fuse', 'fuse_hwstamp',
    ]
    abort_order = ['mvustm', 'ustm', 'ustm_longjmp', 'ustm_simple_longjmp']
    timestamp_impls = [i for i in timestamp_order if i in data]
    abort_impls = [i for i in abort_order if i in data]

    if not timestamp_impls and not abort_impls:
        print("Warning: No matching ablation implementations found")
        return

    ts_labels = {
        'mvustm': '$\\mu$STM Split Stamp',
        'mvustm_hwstamp': '$\\mu$STM HW Stamp',
        'mvustm_lzstamp': '$\\mu$STM Lazy Stamp',
        'mvustm_eager': '$\\mu$STM Eager',
        'mvustm_agg_funnel_eager': '$\\mu$STM Aggregating Funnel',
        'fuse': 'Fuse',
        'fuse_hwstamp': 'Fuse HW Stamp',
    }
    abort_labels = {
        'mvustm': 'Multiversioned, deferred aborts',
        'ustm': 'Single-versioned, exceptions',
        'ustm_longjmp': 'Multiversioned, longjmp',
        'ustm_simple_longjmp': 'Single-versioned, longjmp',
    }

    fig, (ax_ts, ax_ab) = plt.subplots(1, 2, figsize=(7, 2.5))

    # Left: Timestamping variants
    for impl in timestamp_impls:
        points = aggregate_points(data[impl])
        x_vals = [p[0] for p in points]
        y_vals = [p[1] for p in points]
        color = IMPL_COLORS.get(impl, '#333333')
        label = ts_labels.get(impl, IMPL_LABELS.get(impl, impl))
        mkr = IMPL_MARKERS.get(impl, 'o')
        ls = IMPL_LINESTYLES.get(impl, '-')
        ax_ts.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=5,
                   color=color, label=label, linestyle=ls)


    ax_ts.set_xticks(x_vals)
    ax_ts.set_xlabel('Update Rate (%)', fontsize=10)
    ax_ts.set_ylabel('Throughput (MOps/s)', fontsize=11)
    ax_ts.set_title('Timestamping Variants', fontsize=11)
    ax_ts.grid(True, alpha=0.3)
    ax_ts.legend(loc='upper right', fontsize=7.5,
        labelspacing=0.2,   # Reduces vertical space between the items
        handlelength=1.0,   # Shortens the colored lines next to the text
        handletextpad=0.4,  # Shrinks the gap between the line and the text
        borderpad=0.3,      # Shrinks the empty space inside the legend box's edges
        markerscale=0.7     # Makes the markers inside the legend 30% smaller than the ones on the plot
    )
    ax_ts.set_ylim(0, ax_ts.get_ylim()[1]*1.1)

    # Right: Abort mechanism variants
    for impl in abort_impls:
        points = aggregate_points(data[impl])
        x_vals = [p[0] for p in points]
        y_vals = [p[1] for p in points]
        color = IMPL_COLORS.get(impl, '#333333')
        label = abort_labels.get(impl, IMPL_LABELS.get(impl, impl))
        mkr = IMPL_MARKERS.get(impl, 'o')
        ls = IMPL_LINESTYLES.get(impl, '-')
        ax_ab.plot(x_vals, y_vals, marker=mkr, linewidth=2, markersize=7,
                   color=color, label=label, linestyle=ls)

    ax_ab.set_xticks(x_vals)
    ax_ab.set_xlabel('Update Rate (%)', fontsize=10)
    #ax_ab.set_ylabel('Throughput (MOps/s)', fontsize=12)
    ax_ab.set_title('Abort Mechanism Variants', fontsize=11)
    ax_ab.grid(True, alpha=0.3)
    ax_ab.legend(loc='best', fontsize=7.5)
    ax_ab.set_ylim(0, ax_ts.get_ylim()[1])

    ax_ab.grid(True, axis='y')
    plt.tick_params(axis='y', which='both', labelleft=False)

    plt.tight_layout()
    plt.savefig(output_dir / f'ablation.{fmt}', dpi=300, bbox_inches='tight', pad_inches=0.015)
    print(f"Generated: machines.{fmt}")
    plt.close()
    print(f"Generated: ablation.{fmt}")
    plt.close()




def plot_machines(machine_dirs, output_dir, fmt='pdf'):
    """
    Bar chart comparing btree throughput across different machines.
    Each machine_dirs entry: (label, dirpath) where dirpath contains a 'geomean' file.
    Bars grouped by machine, one bar per implementation.
    """
    if not machine_dirs:
        print("Warning: No multi-machine data provided")
        return

    machine_impls = ['mvustm', 'fuse', '2plsf', 'multiverse', 'tiny']
    machine_data = {}  # label -> {impl: geomean_mops}
    structs = ['avltree', 'btree', 'treap']

    for label, dirpath in machine_dirs:
        #find file
        found = False
        files = os.listdir(dirpath)
        for f in files:
            if "geomean" in f:
                found = True
                break
        if not found:
            print(f"  Skipping machine '{label}' (no geomean file in {dirpath})")
            continue
        gfile = Path(dirpath) / f

        _, gdata = parse_timing_file(gfile)
        machine_data[label] = {}
        for struct in structs:
            machine_data[label][struct] = {}
            for impl in machine_impls:
                val = gdata.get((struct, impl), 0)
                if val:
                    machine_data[label][struct][impl] = val

    machines = [label for label in machine_data if machine_data[label]]
    if not machines:
        print("Warning: No geomean data found across machines")
        return
    impls_found = set()
    for d in machine_data.values():
        for s in structs:
            impls_found.update(d[s].keys())
    impls = [i for i in machine_impls if i in impls_found]

    fig, ax = plt.subplots(figsize=(7, 2.6))
    x = np.arange(len(machines))
    
    pairs = list(product(structs, impls))
    n_groups = len(pairs)
    n_impls = len(impls)
    n_structs = len(structs)
    width = 0.6 / max(n_groups, 1)

    inner_spacing = 1.5
    group_spacing = 1.5

    normalized_data = {}
    for m in machines:
        normalized_data[m] = {}
        for s_idx, struct in enumerate(structs):
            values = np.array([machine_data[m][struct].get(impl, 0) for impl in impls])
            normalized = values / max(values)
            normalized_data[m][struct] = {
                impl: normalized[i]
                for i, impl in enumerate(impls)
            }

    bars = []
    for s_idx, struct in enumerate(structs):
        group_offset = (s_idx - (n_structs - 1)/2) * width * n_impls * group_spacing
        for i_idx, impl in enumerate(impls):
            inner_offset = (i_idx - (n_impls - 1)/2) * width * inner_spacing
            offset = group_offset + inner_offset

            values = [normalized_data[m][struct].get(impl, 0) for m in machines]
            values_notnormalized = [machine_data[m][struct].get(impl, -1) for m in machines]

            color = IMPL_COLORS.get(impl, '#333333')
            label = IMPL_LABELS.get(impl, impl)
            hatch = STRUCT_HATCHES.get(struct, None)
            bars.append((values, values_notnormalized,
                   ax.bar(x + offset, values, width,
                   color=color,
                   edgecolor='black', linewidth=1)))

    for values, values_notnormalized, bar in bars:
        for b,v,vnn in zip(bar, values, values_notnormalized):
            ax.text(b.get_x() + b.get_width() / 2,
                b.get_height(),
                f'{vnn:.1f}' if vnn != -1 else "",
                ha='center',
                va='bottom',
                fontsize=fontsize-3)

    ax.set_ylim(0, 1.23)
    ax.set_ylabel('Geomean Throughput (MOps/s)', fontsize=fontsize-2.5)
    ax.tick_params(axis='y', labelsize=fontsize-2)
    #ax.set_title('Geomean Throughput Across Machines', fontsize=14)
    #ax.legend(loc='upper right', fontsize=fontsize-2)
    ax.grid(axis='y', alpha=0.3)

    ax.set_xticks(x)
    ax.set_xticklabels(machines, ha='center', fontsize=fontsize-1)
    ax.xaxis.set_ticks_position('top')
    ax.xaxis.set_label_position('top')
    ax.tick_params(axis='x', top=True, labeltop=True, pad=-14, length=0)
    
    #draw separators
    ymin, ymax = ax.get_ylim()
    separator_width = width * 0.5
    for xi in x:
        for s_idx in range(len(structs) - 1):
            right_edge = (
                xi
                + (s_idx + 0.5 - (len(structs)-1)/2)
                * width * 3.2 * n_impls * group_spacing)
            rect = patches.Rectangle(
                (right_edge - separator_width/2, ymin),
                separator_width,
                ymax - ymin,
                color='black',
                alpha=0.08,
                linewidth=0
            )
            ax.add_patch(rect)

    #data structure labels
    ymax = ax.get_ylim()[1]
    for xi in x:                         # for each machine
        for s_idx, struct in enumerate(structs):
            group_offset = (
                (s_idx - (n_structs - 1)/2)
                * width * n_impls * group_spacing)
            struct_center = xi + group_offset
            ax.text(struct_center,
                    ymax * -.07,
                    STRUCT_LABELS.get(struct, struct),
                    ha='center',
                    va='bottom',
                    fontsize=fontsize-3)

    #stm labels
    impl_handles = [
        Patch(facecolor=IMPL_COLORS.get(i, '#333333'), edgecolor="black", label=IMPL_LABELS.get(i, i))
        for i in impls
    ]
    ax.legend(
        handles=impl_handles,
        bbox_to_anchor=(0.5, 1.25),
        frameon=True,
        fontsize=fontsize-2.5,
        ncol=n_impls,
        loc="upper center",
        alignment="center"
    )

    plt.tight_layout()
    plt.savefig(output_dir / f'machines.{fmt}', dpi=300, bbox_inches='tight', pad_inches=0.022)
    print(f"Generated: machines.{fmt}")
    plt.close()


def main():
    parser = argparse.ArgumentParser(description='Plot paper experiments')
    parser.add_argument('input_dir', type=str, 
                       help='Path to directory containing timing files (geomean, vary_size, etc.)')
    parser.add_argument('-o', '--output-dir', type=str, default='plots',
                       help='Output directory for plots (default: plots)')
    parser.add_argument('--plots', nargs='+', 
                       choices=['geomean', 'varying', 'range', 'combined', 'tpcc',
                                'nontrivial', 'ablation', 'machines', 'paper', 'all'],
                       default=['all'],
                       help='Which plots to generate (default: all)')
    parser.add_argument('--format', type=str, choices=['pdf', 'png', 'svg'], 
                       default='pdf', help='Output format (default: pdf)')
    parser.add_argument('--machine-dirs', nargs='+', metavar='LABEL:DIR',
                       help='Machine data dirs for multi-machine plot (e.g. Intel:/path/to/dir AMD:/path/to/dir)')
    
    args = parser.parse_args()
    
    input_dir = Path(args.input_dir)
    if not input_dir.exists():
        print(f"Error: Path '{args.input_dir}' not found")
        return 1
    if not input_dir.is_dir():
        print(f"Error: '{args.input_dir}' is not a directory")
        return 1
    
    output_dir = Path(args.output_dir)
    output_dir.mkdir(exist_ok=True)
    
    # Set matplotlib backend for file output
    plt.switch_backend('Agg')
    
    print(f"Processing directory: {input_dir}")
    
    exps = [
        'geomean',
        'vary_size',
        'vary_threads',
        'vary_zipfian',
        'vary_trans',
        'vary_update',
        'range_queries',
        'nontrivial_types',
        'ablation',
    ]
    
    # Keep results separate by experiment file
    exp_results = {}
    exp_geomeans = {}  # per-file geomean data
    geomean_data = {}  # combined (for non-geomean uses)
    tpcc_results = []
    
    files = os.listdir(input_dir)

    for filename in exps:
        for f in files:
            if filename in f:
                filepath = input_dir / f
                print(f"  Parsing {filename}...")
                results, gdata = parse_timing_file(filepath)
                exp_results[filename] = results
                exp_geomeans[filename] = gdata
                geomean_data.update(gdata)
                print(f"    Found {len(results)} results, {len(gdata)} geomeans")
                break
        else:
            print(f"  Skipping {filename} (not found)")
    
    # Parse TPCC separately (different format)
    tpcc_path = input_dir / 'tpcc'
    if tpcc_path.exists():
        print(f"  Parsing tpcc...")
        tpcc_results = parse_tpcc_file(tpcc_path)
        print(f"    Found {len(tpcc_results)} TPCC results")
    else:
        print(f"  Skipping tpcc (not found)")
    
    total_results = sum(len(r) for r in exp_results.values()) + len(tpcc_results)
    print(f"\nTotal: {total_results} benchmark results, {len(geomean_data)} geometric mean summaries")
    
    if not total_results and not geomean_data:
        print("Error: No data found")
        return 1
    
    plots = args.plots
    if 'all' in plots:
        plots = ['geomean', 'varying', 'range', 'combined', 'tpcc', 'nontrivial', 'ablation', 'machines', 'paper']
    
    if 'geomean' in plots:
        # Use only geomean data from the dedicated 'geomean' experiment file
        geomean_only = exp_geomeans.get('geomean', {})
        if geomean_only:
            print("\nGenerating Geomean bar chart...")
            plot_geomean_bars(geomean_only, output_dir, 
                                      f'geomean.{args.format}')
    
    if 'varying' in plots:
        print("\nGenerating Parameter variation plots...")
        file_to_param = {
            'vary_size': 'n',
            'vary_threads': 'p',
            'vary_zipfian': 'z',
            'vary_trans': 'trans',
            'vary_update': 'u',
        }
        for exp_file, param in file_to_param.items():
            if exp_file in exp_results and exp_results[exp_file]:
                plot_varying(exp_results[exp_file], output_dir, param)
    
    if 'range' in plots:
        print("\nGenerating Range Query plots...")
        if 'range_queries' in exp_results and exp_results['range_queries']:
            plot_range_queries(exp_results['range_queries'], output_dir)
    
    if 'combined' in plots:
        print("\nGenerating Combined grid plot...")
        plot_combined_grid(exp_results, geomean_data, output_dir, 
                          f'results_combined.{args.format}')
    
    if 'tpcc' in plots:
        print("\nGenerating TPCC bar chart...")
        if tpcc_results:
            plot_tpcc_bars(tpcc_results, output_dir, f'tpcc.{args.format}')
        else:
            print("  No TPCC data found")
    
    if 'nontrivial' in plots:
        print("\nGenerating Non-trivial types plots...")
        if 'nontrivial_types' in exp_results and exp_results['nontrivial_types']:
            plot_nontrivial_types(exp_results['nontrivial_types'], output_dir, args.format)
        else:
            print("  No nontrivial_types data found")
    
    if 'ablation' in plots:
        print("\nGenerating Ablation study plots...")
        if 'ablation' in exp_results and exp_results['ablation']:
            plot_ablation(exp_results['ablation'], output_dir, args.format)
        else:
            print("  No ablation data found")
    
    if 'paper' in plots:
        print("\nGenerating compact paper figures (ART, B-tree, Linked List)...")
        plot_paper_figures(exp_results, output_dir, fmt=args.format)

    if 'machines' in plots and args.machine_dirs:
        print("\nGenerating Multi-machine comparison plot...")
        machine_dirs = []
        for spec in args.machine_dirs:
            if ':' in spec:
                label, dirpath = spec.split(':', 1)
                machine_dirs.append((label, dirpath))
            else:
                label = Path(spec).name
                machine_dirs.append((label, spec))
        plot_machines(machine_dirs, output_dir, args.format)
    
    print(f"\nAll plots saved to {output_dir}/")
    return 0


if __name__ == '__main__':
    sys.exit(main())
