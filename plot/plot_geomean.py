#!/usr/bin/env python3
"""
Parse timing data and produce a multibar chart of geomean throughput
by data structure and implementation.
"""

import re
import sys
import matplotlib.pyplot as plt
import numpy as np
from collections import defaultdict

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

def parse_timings(filename):
    """Parse the timing file and extract geomean throughputs."""
    data = defaultdict(dict)  # data[structure][impl] = geomean
    
    # Pattern to match geomean lines like:
    # ./btree_mvustm,GEOMETRIC MEAN OF THROUGHPUTS: 123.7
    # ./hash_block_mvustm,GEOMETRIC MEAN OF THROUGHPUTS: 263.8
    geomean_pattern = re.compile(r'^\./(.+),GEOMETRIC MEAN OF THROUGHPUTS:\s*([\d.eE+-]+)')
    
    # Pattern for hytm-style output:
    # btree
    # ...results...
    # geomean throughput: 166.443
    hytm_geomean_pattern = re.compile(r'^geomean throughput:\s*([\d.eE+-]+)', re.IGNORECASE)
    
    # Pattern for new compact format:
    # btree,mvustm,geomean=87.4675
    compact_geomean_pattern = re.compile(r'^(\w+),(\w+),geomean=([\d.eE+-]+)')
    
    line_num = 0
    current_struct = None
    with open(filename, 'r') as f:
        for line in f:
            line_num += 1
            
            # Check for compact geomean format (btree,mvustm,geomean=87.4675)
            compact_match = compact_geomean_pattern.match(line)
            if compact_match:
                structure = compact_match.group(1)
                impl = compact_match.group(2)
                try:
                    geomean = float(compact_match.group(3))
                except ValueError as e:
                    print(f"ERROR: Failed to parse compact geomean value at line {line_num}: {line.strip()}")
                    sys.exit(1)
                data[structure][impl] = geomean
                continue
            
            # Check for standard geomean format
            match = geomean_pattern.match(line)
            if match:
                name = match.group(1)
                structure, impl = parse_structure_impl(name)
                try:
                    geomean = float(match.group(2))
                except ValueError as e:
                    print(f"ERROR: Failed to parse geomean value at line {line_num}: {line.strip()}")
                    sys.exit(1)
                data[structure][impl] = geomean
                continue
            
            # Check for hytm-style geomean format
            hytm_match = hytm_geomean_pattern.match(line)
            if hytm_match and current_struct:
                try:
                    geomean = float(hytm_match.group(1))
                except ValueError as e:
                    print(f"ERROR: Failed to parse hytm geomean value at line {line_num}: {line.strip()}")
                    sys.exit(1)
                data[current_struct]['hytm'] = geomean
                current_struct = None
                continue
            
            # Track current structure for hytm-style files (structure name on its own line)
            stripped = line.strip()
            if stripped and not stripped.startswith('./') and not stripped.startswith('Date:') and \
               not stripped.startswith('Model') and not stripped.startswith('Git') and \
               not stripped.startswith('Compiler:') and not stripped.startswith('OS:') and \
               not stripped.startswith('Hugepage:') and not stripped.startswith('Threads') and \
               not stripped.startswith('HYTM') and '=' not in stripped and 'range queries' not in stripped:
                # This might be a structure name
                current_struct = stripped
            
            if 'GEOMETRIC MEAN' in line and not geomean_pattern.match(line):
                # Geomean line that didn't match our pattern - parsing error
                print(f"ERROR: Failed to parse geomean line {line_num}: {line.strip()}")
                sys.exit(1)
    
    if not data:
        print(f"ERROR: No geomean data found in {filename}")
        sys.exit(1)
    
    return data

def plot_geomean_chart(data, output_file='geomean_chart.png', normalize=True, baseline='mvustm'):
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
    
    # Normalize data if requested
    if normalize:
        normalized_data = defaultdict(dict)
        for struct in structures:
            # Find max value for this structure
            max_val = max(data[struct].values()) if data[struct] else 1
            if max_val == 0:
                print(f"WARNING: Max value is 0 for {struct}, skipping")
                continue
            for impl in implementations:
                if impl in data[struct]:
                    normalized_data[struct][impl] = data[struct][impl] / max_val
        plot_data = normalized_data
        structures = [s for s in structures if s in normalized_data]  # Filter out skipped structures
        ylabel = 'Normalized Throughput (relative to max per structure)'
        title = 'Normalized Geomean Throughput by Data Structure (baseline: max per group)'
    else:
        plot_data = data
        ylabel = 'Geomean Throughput (mops)'
        title = 'Geomean Throughput by Data Structure and Implementation'
    
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
    print(f"Chart saved to {output_file}")
    plt.show()

def main():
    if len(sys.argv) < 2:
        filenames = ['../timings/144-40GHz_12-30-2025']
    else:
        filenames = sys.argv[1:]
    
    # Parse all files and merge data
    data = defaultdict(dict)
    for filename in filenames:
        print(f"Parsing {filename}...")
        file_data = parse_timings(filename)
        
        # Check if this is an hytm-specific file
        is_hytm_file = 'hytm' in filename
        
        for struct, impl_data in file_data.items():
            for impl, value in impl_data.items():
                # Skip hytm data from non-hytm files
                if impl == 'hytm' and not is_hytm_file:
                    continue
                if impl in data[struct]:
                    print(f"WARNING: Duplicate {struct}/{impl}, using value from {filename}")
                data[struct][impl] = value
    
    # Print summary
    print(f"\nFound {len(data)} data structures:")
    for struct in sorted(data.keys()):
        impls = list(data[struct].keys())
        print(f"  {struct}: {len(impls)} implementations")
        for impl in impls:
            print(f"    {impl}: {data[struct][impl]:.1f} mops")
    
    # Generate chart
    output_file = filenames[0].split('/')[-1] + '_geomean.png'
    plot_geomean_chart(data, output_file)

if __name__ == '__main__':
    main()
