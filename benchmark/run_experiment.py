#!/usr/bin/env python3
"""
Usage:
    ./run_experiment.py <experiment> [options]

Experiments:
    geomean   - Geometric mean across parameter sweep
    size      - Vary data structure size
    threads   - Vary thread count
    zipfian   - Vary Zipfian skew parameter
    trans     - Vary transaction length
    update    - Vary update percentage
    range     - Range query experiments
    all       - Run all experiments

Options:
    --no-hytm     Exclude HyTM (allows using all threads/sockets)
    --output DIR  Output directory for results

Implementations tested:
    mvustm, mvustm_hwstamp, mvustm_longjmp, 2plsf, fuse, hytm, onfile, tinystm, tl2

Examples:
    ./run_experiment.py geomean
    ./run_experiment.py all --no-hytm
    ./run_experiment.py threads --output ./results
"""

import argparse
import subprocess
import os
import re
import sys
from datetime import datetime
from dataclasses import dataclass
from typing import List, Optional
from math import prod

@dataclass
class Config:
    """Experiment configuration."""
    # Data structures
    structs: List[str] = None
    structs_nohash: List[str] = None
    lstructs: List[str] = None
    range_structs: List[str] = None
    
    # Implementations
    impls: List[str] = None
    
    # Execution settings
    max_threads: int = 72
    numa_cmd: str = "numactl --cpunodebind=0,1 --membind=0,1"
    
    # Default parameters
    default_n: int = 100000
    default_list_n: int = 300
    default_u: int = 5
    default_trans: int = 4
    default_z: float = 0.9
    default_p: int = 72
    rounds: int = 3
    trial_time: int = 1
    
    def __post_init__(self):
        self.structs = ["btree", "arttree", "avltree", "leaftree", "treap", "skiplist", "hash_block"]
        self.structs_nohash = ["btree", "arttree", "avltree", "leaftree", "treap", "skiplist"]
        self.lstructs = ["list"]
        self.range_structs = ["btree", "arttree"]


def get_config(include_hytm: bool) -> Config:
    config = Config()
    
    if include_hytm:
        config.impls = ["mvustm", "mvustm_hwstamp", "mvustm_longjmp", "2plsf", "fuse", "hytm", "onefile", "tiny", "tl2"]
        config.max_threads = 72
        config.numa_cmd = "numactl --cpunodebind=0,1 --membind=0,1"
    else:
        config.impls = ["mvustm", "mvustm_hwstamp", "mvustm_longjmp", "2plsf", "fuse"]
        config.max_threads = 144
        config.numa_cmd = "numactl -i all"
    
    config.default_p = config.max_threads
    return config


def get_nproc() -> int:
    try:
        return os.cpu_count() or 1
    except:
        return 1


def get_machine_info() -> str:
    try:
        result = subprocess.run(["lscpu"], capture_output=True, text=True)
        for line in result.stdout.split('\n'):
            if 'Model name' in line:
                # Get last 5 chars of model name
                return line.strip()[-5:].replace(' ', '')
    except:
        pass
    return "unknown"


def geomean(values: List[float]) -> float:
    values = [v for v in values if v and v > 0]
    if not values:
        return 0.0
    return prod(values) ** (1.0 / len(values))


def run_benchmark(cmd: str, numa_cmd: str) -> str:
    full_cmd = f"{numa_cmd} {cmd}"
    try:
        result = subprocess.run(
            full_cmd, shell=True, capture_output=True, text=True, timeout=300
        )
        return result.stdout + result.stderr
    except subprocess.TimeoutExpired:
        return ""
    except Exception as e:
        print(f"  Error running: {cmd}: {e}", file=sys.stderr)
        return ""


def parse_mops(output: str) -> Optional[float]:
    """Extract thrughput from benchmark output."""
    match = re.search(r'mops=([\d.]+)', output)
    return float(match.group(1)) if match else None


def get_result_line(output: str) -> Optional[str]:
    """Extract result line"""
    for line in output.split('\n'):
        if 'mops=' in line:
            return line.strip()
    return None


def get_structs_for_impl(config: Config, impl: str) -> List[str]:
    """Get structures compatible with implementation."""
    if impl == "hytm":
        return config.structs_nohash
    if impl == "fuse":
        # Fuse/TLF only supports pointer and bool atomics, not int atomics
        # Exclude: avltree (uses atomic<int> for height), hash_block (uses atomic<V>)
        return [s for s in config.structs if s not in ["avltree", "hash_block"]]
    return config.structs


class ExperimentRunner:
    
    def __init__(self, config: Config, output_dir: Optional[str] = None):
        self.config = config
        self.output_dir = output_dir
        self.nproc = get_nproc()
        self.results_file = None
        
    def setup_output(self, experiment_name: str) -> str:
        """Create new timings file"""
        today = datetime.now().strftime("%m-%d-%Y")
        machine = get_machine_info()
        suffix = "_hytm" if "hytm" in self.config.impls else ""
        
        if self.output_dir:
            os.makedirs(self.output_dir, exist_ok=True)
            filename = os.path.join(self.output_dir, experiment_name)
        else:
            filename = f"../../timings/tlf_{experiment_name}_{self.nproc}-{machine}_{today}{suffix}"
        
        print(f"Output: {filename}")
        print(f"HYTM: {'yes' if 'hytm' in self.config.impls else 'no'} (max_threads={self.config.max_threads})")
        
        self.results_file = open(filename, 'w')
        self._write_header(experiment_name, today)
        return filename
    
    def _write_header(self, experiment_name: str, today: str):
        """Write header to results file."""
        self.results_file.write(f"Date: {today}\n")
        self.results_file.write(f"Experiment: {experiment_name}\n")
        self.results_file.write(f"HYTM: {'yes' if 'hytm' in self.config.impls else 'no'}\n")
        
        try:
            result = subprocess.run(["lscpu"], capture_output=True, text=True)
            for line in result.stdout.split('\n'):
                if 'Model name' in line:
                    self.results_file.write(f"{line.strip()}\n")
                    break
        except:
            pass
        
        try:
            result = subprocess.run(["git", "rev-parse", "--short", "HEAD"], 
                                   capture_output=True, text=True)
            self.results_file.write(f"Git: {result.stdout.strip()}\n")
        except:
            pass
        
        self.results_file.write("---\n")
        self.results_file.flush()
    
    def _write_result(self, prefix: str, line: str):
        """Write a result line to output file."""
        self.results_file.write(f"{prefix},{line}\n")
        self.results_file.flush()
    
    def _binary_exists(self, struct: str, impl: str) -> bool:
        """Check if benchmark binary exists."""
        return os.path.isfile(f"./{struct}_{impl}")
    
    def _run_single(self, struct: str, impl: str, params: str) -> Optional[str]:
        """Run a single benchmark and return result line."""
        if not self._binary_exists(struct, impl):
            return None
        
        cmd = f"./{struct}_{impl} {params}"
        output = run_benchmark(cmd, self.config.numa_cmd)
        return get_result_line(output)
    
    def run_geomean(self):
        """Run geometric mean experiment"""
        self.setup_output("geomean")
        self.results_file.write(f"Threads: {self.config.default_p}\n")
        self.results_file.write("---\n")
        
        sizes = [10000, 1000000, 100000000]
        list_sizes = [100, 1000]
        updates = [0, 5, 50]
        trans_vals = [1, 4, 16]
        zipfians = [0, 0.99]
        
        # Comma-separated args for efficient single invocation
        geomean_args = "-n 10000,1000000,100000000 -u 0,5,50 -z 0,0.99 -trans 1,4,16"
        list_geomean_args = "-n 100,1000 -u 0,5,50 -z 0,0.99 -trans 1,4,16"
        
        all_structs = [(s, sizes, geomean_args) for s in self.config.structs]
        all_structs += [(s, list_sizes, list_geomean_args) for s in self.config.lstructs]
        
        for impl in self.config.impls:
            structs_to_run = get_structs_for_impl(self.config, impl)
            
            for struct, sz_list, geomean_arg in all_structs:
                if struct not in structs_to_run and struct not in self.config.lstructs:
                    continue
                if not self._binary_exists(struct, impl):
                    continue
                
                print(f"  {struct}_{impl}")
                prefix = f"./{struct}_{impl}"
                
                if impl == "hytm":
                    # HyTM: separate invocations due to memory leak
                    throughputs = []
                    for n in sz_list:
                        for u in updates:
                            for trans in trans_vals:
                                for z in zipfians:
                                    params = (f"-nowarmup -n {n} -u {u} -z {z} "
                                             f"-r {self.config.rounds} -trans {trans} "
                                             f"-p {self.config.default_p} -t {self.config.trial_time}")
                                    result_line = self._run_single(struct, impl, params)
                                    if result_line:
                                        self._write_result(prefix, result_line)
                                        mops = parse_mops(result_line)
                                        if mops:
                                            throughputs.append(mops)
                    
                    if throughputs:
                        gmean = geomean(throughputs)
                        self._write_result(prefix, f"GEOMETRIC MEAN OF THROUGHPUTS: {gmean:.4f}")
                else:
                    # Non-HyTM: efficient single invocation
                    params = f"{geomean_arg} -r {self.config.rounds} -p {self.config.default_p} -t {self.config.trial_time}"
                    cmd = f"./{struct}_{impl} {params}"
                    output = run_benchmark(cmd, self.config.numa_cmd)
                    
                    for line in output.split('\n'):
                        line = line.strip()
                        if 'mops=' in line or 'GEOMETRIC MEAN' in line:
                            self._write_result(prefix, line)
        
        self.results_file.close()
    
    def _run_varying_experiment(self, name: str, param_name: str, values: List,
                                fixed_params: dict, use_list_n: bool = True):
        """Generic function for varying-parameter experiments."""
        self.setup_output(name)
        
        fixed_str = ", ".join(f"{k}={v}" for k, v in fixed_params.items())
        if use_list_n:
            fixed_str += f" ({self.config.default_list_n} for list)"
        self.results_file.write(f"Fixed: {fixed_str}\n")
        self.results_file.write("---\n")
        
        for impl in self.config.impls:
            structs_to_run = get_structs_for_impl(self.config, impl)
            
            # Regular structures
            for struct in structs_to_run:
                if not self._binary_exists(struct, impl):
                    continue
                print(f"  {struct}_{impl}")
                prefix = f"./{struct}_{impl}"
                
                for val in values:
                    if param_name == 'p' and val > self.nproc:
                        continue
                    
                    params = dict(fixed_params)
                    params[param_name] = val
                    
                    param_str = " ".join(f"-{k} {v}" for k, v in params.items())
                    param_str = f"-nowarmup {param_str} -r {self.config.rounds} -t {self.config.trial_time}"
                    
                    result_line = self._run_single(struct, impl, param_str)
                    if result_line:
                        self._write_result(prefix, result_line)
            
            # List structures (with smaller n)
            for struct in self.config.lstructs:
                if not self._binary_exists(struct, impl):
                    continue
                print(f"  {struct}_{impl}")
                prefix = f"./{struct}_{impl}"
                
                for val in values:
                    if param_name == 'p' and val > self.nproc:
                        continue
                    
                    params = dict(fixed_params)
                    params[param_name] = val
                    if 'n' in params and use_list_n:
                        params['n'] = self.config.default_list_n
                    
                    param_str = " ".join(f"-{k} {v}" for k, v in params.items())
                    param_str = f"-nowarmup {param_str} -r {self.config.rounds} -t {self.config.trial_time}"
                    
                    result_line = self._run_single(struct, impl, param_str)
                    if result_line:
                        self._write_result(prefix, result_line)
        
        self.results_file.close()
    
    def run_size(self):
        sizes = [1000, 10000, 100000, 1000000, 10000000, 100000000]
        list_sizes = [10, 100, 300, 1000, 3000, 10000]
        
        self.setup_output("vary_size")
        self.results_file.write(f"Fixed: u={self.config.default_u}, trans={self.config.default_trans}, "
                               f"z={self.config.default_z}, p={self.config.default_p}\n")
        self.results_file.write("---\n")
        
        for impl in self.config.impls:
            for struct in get_structs_for_impl(self.config, impl):
                if not self._binary_exists(struct, impl):
                    continue
                print(f"  {struct}_{impl}")
                
                for n in sizes:
                    params = (f"-nowarmup -n {n} -u {self.config.default_u} -z {self.config.default_z} "
                             f"-r {self.config.rounds} -trans {self.config.default_trans} "
                             f"-p {self.config.default_p} -t {self.config.trial_time}")
                    result_line = self._run_single(struct, impl, params)
                    if result_line:
                        self._write_result(f"./{struct}_{impl}", result_line)
            
            for struct in self.config.lstructs:
                if not self._binary_exists(struct, impl):
                    continue
                print(f"  {struct}_{impl}")
                
                for n in list_sizes:
                    params = (f"-nowarmup -n {n} -u {self.config.default_u} -z {self.config.default_z} "
                             f"-r {self.config.rounds} -trans {self.config.default_trans} "
                             f"-p {self.config.default_p} -t {self.config.trial_time}")
                    result_line = self._run_single(struct, impl, params)
                    if result_line:
                        self._write_result(f"./{struct}_{impl}", result_line)
        
        self.results_file.close()
    
    def run_threads(self):
        if "hytm" in self.config.impls:
            threads = [1, 4, 8, 16, 32, 48, 64, 72]
        else:
            threads = [1, 4, 8, 16, 32, 48, 64, 96, 128, 144]
        
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'trans': self.config.default_trans,
            'z': self.config.default_z,
        }
        self._run_varying_experiment("vary_threads", 'p', threads, fixed)
    
    def run_zipfian(self):
        zipfians = [0, 0.25, 0.5, 0.75, 0.9, 0.95, 0.99]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'trans': self.config.default_trans,
            'p': self.config.default_p,
        }
        self._run_varying_experiment("vary_zipfian", 'z', zipfians, fixed)
    
    def run_trans(self):
        trans_vals = [1, 2, 4, 8, 16, 32]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'z': self.config.default_z,
            'p': self.config.default_p,
        }
        self._run_varying_experiment("vary_trans", 'trans', trans_vals, fixed)
    
    def run_update(self):
        updates = [0, 1, 5, 10, 20, 50]
        fixed = {
            'n': self.config.default_n,
            'trans': self.config.default_trans,
            'z': self.config.default_z,
            'p': self.config.default_p,
        }
        self._run_varying_experiment("vary_update", 'u', updates, fixed)
    
    def run_range(self):
        self.setup_output("range_queries")
        
        range_n = 1000000
        range_u = 100
        range_trans = 4
        range_z = 0
        range_rqp = 50
        range_sizes = [1, 10, 100, 1000, 10000, 100000, 1000000]
        
        self.results_file.write(f"Fixed: n={range_n}, u={range_u}, trans={range_trans}, "
                               f"z={range_z}, p={self.config.default_p}, rqp={range_rqp}\n")
        self.results_file.write("---\n")
        
        for impl in self.config.impls:
            for struct in self.config.range_structs:
                if not self._binary_exists(struct, impl):
                    continue
                print(f"  {struct}_{impl}")
                prefix = f"./{struct}_{impl}"
                
                for rs in range_sizes:
                    params = (f"-n {range_n} -u {range_u} -z {range_z} -trans {range_trans} "
                             f"-rs {rs} -rqp {range_rqp} -p {self.config.default_p} "
                             f"-t {self.config.trial_time}")
                    result_line = self._run_single(struct, impl, params)
                    if result_line:
                        self._write_result(prefix, result_line)
        
        self.results_file.close()
    
    def run_all(self):
        """Run all experiments."""
        print("=" * 50)
        print("Running all TLF experiments")
        print("=" * 50)
        
        experiments = [
            ("Experiment 1: Geometric Mean", self.run_geomean),
            ("Experiment 2: Varying Size", self.run_size),
            ("Experiment 3: Varying Threads", self.run_threads),
            ("Experiment 4: Varying Zipfian", self.run_zipfian),
            ("Experiment 5: Varying Transaction Size", self.run_trans),
            ("Experiment 6: Varying Update Rate", self.run_update),
            ("Experiment 7: Range Queries", self.run_range),
        ]
        
        for name, func in experiments:
            print(f"\n=== {name} ===")
            func()
        
        print("\n" + "=" * 50)
        print("All experiments complete!")
        print("=" * 50)


def main():
    parser = argparse.ArgumentParser(
        description="Experiment Runner",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Experiments:
  geomean    Geometric mean performance
  size       Varying data structure size
  threads    Varying thread count
  zipfian    Varying zipfian skew 
  trans      Varying transaction size
  update     Varying update rate
  range      Range queries
  all        Run all experiments

Examples:
  ./run_experiment.py geomean
  ./run_experiment.py all --no-hytm
  ./run_experiment.py threads --output ./results
""")
    
    parser.add_argument("experiment", 
                       choices=["geomean", "size", "threads", "zipfian", "trans", "update", "range", "all"],
                       help="Experiment to run")
    parser.add_argument("--no-hytm", action="store_true",
                       help="Exclude HyTM (use all available threads/sockets)")
    parser.add_argument("--output", "-o", metavar="DIR",
                       help="Output directory for results")
    
    args = parser.parse_args()
    
    config = get_config(include_hytm=not args.no_hytm)
    runner = ExperimentRunner(config, args.output)
    
    experiments = {
        "geomean": runner.run_geomean,
        "size": runner.run_size,
        "threads": runner.run_threads,
        "zipfian": runner.run_zipfian,
        "trans": runner.run_trans,
        "update": runner.run_update,
        "range": runner.run_range,
        "all": runner.run_all,
    }
    
    experiments[args.experiment]()


if __name__ == "__main__":
    main()
