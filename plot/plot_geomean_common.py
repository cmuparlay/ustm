#!/usr/bin/env python3
"""
Parse timing data and produce a multibar chart of geomean throughput
by data structure and implementation.

This version computes geomean only over configurations that succeeded
for ALL implementations, making comparisons fair.
"""

import re
import sys
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict
from scipy import stats

# Fixed colors for each implementation (consistent across all charts)
IMPL_COLORS = {
    'mvustm': '#1f77b4',
    'mvustm_lzstamp': '#ff7f0e',
    'mvustm_hwstamp': '#2ca02c',
    'ustm_longjmp': '#d62728',
    'mvustm_longjmp': '#9467bd',
    'mvustm_trythrow': '#8c564b',
    'mvustm_unprotected': '#e377c2',
    '2plsf': '#7f7f7f',
    'hytm': '#bcbd22',
    'tiny': '#17becf',
}

# Known implementations (order matters - longer matches first)
KNOWN_IMPLS = [
    'mvustm_lzstamp', 'mvustm_hwstamp', 'mvustm_longjmp', 'mvustm_trythrow',
    'mvustm_unprotected', 'ustm_longjmp', 'mvustm', '2plsf', 'hytm', 'tiny'
]

def parse_structure_impl(name):
    """Parse structure_impl string, matching known implementations from the end."""
    for impl in KNOWN_IMPLS:
        if name.endswith('_' + impl):
            struct = name[:-(len(impl) + 1)]
            return struct, impl
    # Fallback: split on first underscore (shouldn't happen with known impls)
    parts = name.split('_', 1)
    if len(parts) == 2:
        return parts[0], parts[1]
    return name, 'unknown'

def parse_individual_runs(filename):
    """Parse individual benchmark runs from a timing file.
    
    Returns: dict[structure][impl][config] = throughput
    where config is a tuple of (n, u, z) parameters
    """
    data = defaultdict(lambda: defaultdict(dict))
    
    # Pattern to match individual run lines like:
    # ./btree_mvustm,u=0,trans=4,n=100000,p=72,z=0,pa=0,mops=255
    run_pattern = re.compile(r'^\./(.+),u=(\d+),trans=\d+,n=(\d+),p=\d+,z=([\d.]+),.*mops=([\d.eE+-]+)')
    
    line_num = 0
    with open(filename, 'r') as f:
        for line in f:
            line_num += 1
            match = run_pattern.match(line)
            if match:
                name = match.group(1)
                u = int(match.group(2))
                n = int(match.group(3))
                z = float(match.group(4))
                mops = float(match.group(5))
                
                structure, impl = parse_structure_impl(name)
                config = (n, u, z)
                data[structure][impl][config] = mops
    
    return data

def parse_hytm_runs(filename):
    """Parse individual hytm benchmark runs from an hytm timing file.
    
    Returns: dict[structure][config] = throughput
    where config is a tuple of (n, u, z) parameters
    """
    data = defaultdict(dict)
    
    # Pattern to match hytm run lines like:
    # ./btree_hytm,u=0,trans=4,n=100000,p=72,z=0,pa=0,mops=255
    run_pattern = re.compile(r'^\./(.+)_hytm,u=(\d+),trans=\d+,n=(\d+),p=\d+,z=([\d.]+),.*mops=([\d.eE+-]+)')
    
    with open(filename, 'r') as f:
        for line in f:
            match = run_pattern.match(line)
            if match:
                structure = match.group(1)
                u = int(match.group(2))
                n = int(match.group(3))
                z = float(match.group(4))
                mops = float(match.group(5))
                
                config = (n, u, z)
                data[structure][config] = mops
    
    return data

def compute_geomean(values):
    """Compute geometric mean of a list of values."""
    if not values:
        return 0
    return stats.gmean(values)

def compute_common_geomeans(data, hytm_data=None):
    """Compute geomeans only over configurations common to all implementations.
    
    Returns: dict[structure][impl] = geomean
    """
    result = defaultdict(dict)
    
    for structure, impl_data in data.items():
        # Get all implementations for this structure
        implementations = list(impl_data.keys())
        
        # Add hytm if we have data for this structure
        if hytm_data and structure in hytm_data:
            implementations.append('hytm')
        
        if not implementations:
            continue
        
        # Find configurations common to ALL implementations
        common_configs = None
        for impl in implementations:
            if impl == 'hytm':
                configs = set(hytm_data[structure].keys())
            else:
                configs = set(impl_data[impl].keys())
            
            if common_configs is None:
                common_configs = configs
            else:
                common_configs = common_configs & configs
        
        if not common_configs:
            print(f"WARNING: No common configurations for {structure}, skipping")
            continue
        
        print(f"{structure}: {len(common_configs)} common configurations across {len(implementations)} implementations")
        for config in sorted(common_configs):
            print(f"  n={config[0]}, u={config[1]}, z={config[2]}")
        
        # Compute geomean for each implementation using only common configs
        for impl in implementations:
            if impl == 'hytm':
                values = [hytm_data[structure][c] for c in common_configs]
            else:
                values = [impl_data[impl][c] for c in common_configs]
            
            result[structure][impl] = compute_geomean(values)
    
    return result

def plot_geomean_chart(data, output_file='geomean_chart.png', normalize=True):
    """Create a grouped bar chart of geomean throughputs."""
    
    # Get all structures and implementations
    structures = sorted(data.keys())
    all_impls = set()
    for struct_data in data.values():
        all_impls.update(struct_data.keys())
    
    # Sort implementations for consistent ordering
    impl_order = [
        'mvustm', 'mvustm_lzstamp', 'mvustm_hwstamp', 
        'ustm_longjmp', 'mvustm_longjmp', 'mvustm_trythrow', 
        'mvustm_unprotected', '2plsf', 'hytm', 'tiny'
    ]
    implementations = [impl for impl in impl_order if impl in all_impls]
    # Add any remaining implementations not in our predefined order
    implementations.extend([impl for impl in sorted(all_impls) if impl not in implementations])
    
    # Normalize data if requested (by max per group)
    if normalize:
        normalized_data = defaultdict(dict)
        for struct in structures:
            max_val = max(data[struct].values()) if data[struct] else 1
            if max_val == 0:
                print(f"WARNING: Max value is 0 for {struct}, skipping")
                continue
            for impl in implementations:
                if impl in data[struct]:
                    normalized_data[struct][impl] = data[struct][impl] / max_val
        plot_data = normalized_data
        structures = [s for s in structures if s in normalized_data]
        ylabel = 'Normalized Throughput (relative to max per structure)'
        title = 'Normalized Geomean Throughput (common configs only)'
    else:
        plot_data = data
        ylabel = 'Geomean Throughput (mops)'
        title = 'Geomean Throughput by Data Structure (common configs only)'
    
    # Create figure
    fig, ax = plt.subplots(figsize=(16, 8))
    
    # Set up bar positions
    x = np.arange(len(structures))
    n_impls = len(implementations)
    width = 0.8 / n_impls
    
    # Assign colors to any unknown implementations
    extra_colors = plt.cm.tab20(np.linspace(0, 1, 20))
    color_idx = 0
    for impl in implementations:
        if impl not in IMPL_COLORS:
            print(f"WARNING: Unknown implementation '{impl}', assigning color")
            IMPL_COLORS[impl] = extra_colors[color_idx]
            color_idx += 1
    
    # Plot bars for each implementation with consistent colors
    for i, impl in enumerate(implementations):
        values = [plot_data[struct].get(impl, 0) for struct in structures]
        offset = (i - n_impls/2 + 0.5) * width
        color = IMPL_COLORS[impl]
        bars = ax.bar(x + offset, values, width, label=impl, color=color)
    
    # Add horizontal line at 1.0 for normalized plot
    if normalize:
        ax.axhline(y=1.0, color='black', linestyle='--', linewidth=1, alpha=0.7)
    
    # Customize chart
    ax.set_xlabel('Data Structure', fontsize=12)
    ax.set_ylabel(ylabel, fontsize=12)
    ax.set_title(title, fontsize=14)
    ax.set_xticks(x)
    ax.set_xticklabels(structures, rotation=45, ha='right')
    ax.legend(loc='upper right', fontsize=9, ncol=2)
    ax.grid(axis='y', alpha=0.3)
    
    # Adjust layout
    plt.tight_layout()
    
    # Save and show
    plt.savefig(output_file, dpi=150, bbox_inches='tight')
    print(f"\nChart saved to {output_file}")
    plt.show()

def main():
    if len(sys.argv) < 2:
        filenames = ['../timings/144-40GHz_12-30-2025']
    else:
        filenames = sys.argv[1:]
    
    # Parse all files and merge individual run data
    all_runs = defaultdict(lambda: defaultdict(dict))
    hytm_runs = defaultdict(dict)
    
    for filename in filenames:
        print(f"Parsing {filename}...")
        
        is_hytm_file = 'hytm' in filename
        
        if is_hytm_file:
            # Parse hytm-specific file
            file_hytm = parse_hytm_runs(filename)
            for struct, configs in file_hytm.items():
                for config, value in configs.items():
                    hytm_runs[struct][config] = value
        else:
            # Parse standard file (skip hytm data from it)
            file_runs = parse_individual_runs(filename)
            for struct, impl_data in file_runs.items():
                for impl, configs in impl_data.items():
                    if impl == 'hytm':
                        continue  # Skip hytm from non-hytm files
                    for config, value in configs.items():
                        all_runs[struct][impl][config] = value
    
    # Compute geomeans over common configurations
    print("\nComputing geomeans over common configurations...")
    geomeans = compute_common_geomeans(all_runs, hytm_runs if hytm_runs else None)
    
    # Print summary
    print(f"\nGeomean results:")
    for struct in sorted(geomeans.keys()):
        print(f"  {struct}:")
        for impl, value in geomeans[struct].items():
            print(f"    {impl}: {value:.1f} mops")
    
    # Generate chart
    output_file = 'geomean_common_configs.png'
    plot_geomean_chart(geomeans, output_file)

if __name__ == '__main__':
    main()
