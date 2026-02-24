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
    tpcc      - TPCC benchmark experiments
    all       - Run all experiments

Options:
    --hytm        Include HyTM (limits to 72 threads on 2 sockets)
    --output DIR  Output directory for results
    --small       Restrict to btree, hash_block, and list (geomean uses runall.sh parameters)

Implementations tested:
    mvustm, mvustm_hwstamp, mvustm_longjmp, 2plsf, fuse, hytm, onfile, tinystm, tl2

Examples:
    ./run_experiment.py geomean
    ./run_experiment.py all --hytm
    ./run_experiment.py threads --output ./results
"""

import argparse
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from datetime import datetime
from math import prod
from typing import List, Optional, Tuple

@dataclass
class Config:
    def __init__(self, args):
        self.small = args.small
        if args.impl == None:
            self.impls = [
                "multiverse",
                "mvustm",
                # "mvustm_hwstamp",
                # "mvustm_longjmp",
                "2plsf",
                "fuse",
                # "onefile",
                # "tiny",
                # "tl2",
                # DCOTL is broken for most benchmarks
                # "dcotl"
            ]
            self.tpcc_impls = ["mvustm", "fuse", "2plsf", "multiverse"]
        else:
            #only run impl
            self.impls = self.tpcc_impls = [ args.impl ]

        if args.hytm:
            self.impls.append("hytm")
            self.max_threads = 72
            self.numa_cmd = "numactl --cpunodebind=0,1 --membind=0,1"
        else:
            self.max_threads = os.cpu_count()
            self.numa_cmd = "numactl -i all"
        self.default_p = self.max_threads
        self.allocator = args.allocator

        if args.struct == None:
            self.structs = ["btree", "arttree", "avltree", "leaftree", "treap", "skiplist", "hash_block"]
            self.structs_nohash = ["btree", "arttree", "avltree", "leaftree", "treap", "skiplist"]
            self.lstructs = ["list"]
            self.range_structs = ["btree", "arttree"]
            self.small_structs = ["btree", "hash_list"]
            self.tpcc_structs = ["hash_list", "btree", "mix"]
        else:
            self.structs = self.structs_nohash = self.lstructs = \
                self.range_structs = self.small_structs = self.tpcc_structs = [ args.struct ]
        print(self)

    """Experiment configuration."""
    # Data structures
    structs: List[str] = None
    structs_nohash: List[str] = None
    lstructs: List[str] = None
    range_structs: List[str] = None
    small_structs: List[str] = None
    tpcc_structs: List[str] = None
    tpcc_impls: List[str] = None
    
    # Implementations
    impls: List[str] = None
    
    # Execution settings
    max_threads: int = os.cpu_count()
    numa_cmd: str = "numactl -i all"
    small: bool = False
    
    # Default parameters
    default_n: int = 10 ** 6
    default_list_n: int = 300
    default_u: int = 5
    default_trans: int = 4
    default_z: float = 0.75
    default_p: int = os.cpu_count()
    rounds: int = 1
    trial_time: int = 1

    allocator: str = "deqalloc"


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


def run_benchmark(cmd: str, numa_cmd: str, allocator: str, timeout_sec: int = 600) -> Tuple[str, str]:
    """
    Run a benchmark command with timeout.
    Returns (output, status) where status is 'ok', 'timeout', 'crash', or 'error'.
    """
    rss_cmd = "/usr/bin/time -f \"Peak RSS: %M KB\"" if sys.platform == "linux" else ""
    if allocator == "deqalloc":
        allocator_path = "../libdeqalloc.so"
    else:
        #assumes allocator is installed
        allocator_path = f"lib{allocator}.so"
    full_cmd = f"LD_PRELOAD={allocator_path} timeout --signal=KILL {timeout_sec} {numa_cmd} {rss_cmd} {cmd}"
    try:
        proc = subprocess.Popen(full_cmd, \
            shell=True, \
            text=True, \
            stdout=subprocess.PIPE, \
            stderr=subprocess.STDOUT)

        result = []
        for line in proc.stdout:
            print(line, end="")
            result.append(line)

        try:
            proc.communicate(timeout=timeout_sec)
        except subprocess.TimeoutExpired:
            proc.kill()

        returncode = proc.returncode
        output = "".join(result)
        
        if returncode == 124 or returncode == 137:
            # 124 = timeout killed it, 137 = SIGKILL (128 + 9)
            print(f"  TIMEOUT ({timeout_sec}s): {cmd}", file=sys.stderr)
            return output, "timeout"
        elif returncode != 0:
            # Multiverse intentionally aborts after completing all work
            if 'multiverse' in cmd:
                return output, "ok"
            # Report crash even if partial output was produced;
            # callers preserve partial results and log the failure.
            print(f"  CRASH (exit={returncode}): {cmd}", file=sys.stderr)
            return output, "crash"
        
        return output, "ok"
    except Exception as e:
        print(f"  ERROR: {cmd}: {e}", file=sys.stderr)
        return "", "error"

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

def requires_transactional_allocation(impl: str) -> bool:
    return impl in ["hytm", "multiverse", "tiny"]

def get_structs_for_impl(config: Config, impl: str) -> List[str]:
    """Get structures compatible with implementation."""
    if requires_transactional_allocation(impl):
        # Only systems supporting non-transactional allocation work with hash structs
        return config.structs_nohash
    # if impl == "fuse":
        # Exclude: avltree (uses atomic<int> for height), hash_block (uses atomic<V>)
        # return [s for s in config.structs if s not in ["avltree", "hash_block"]]
    return config.structs


def filter_structs_if_small(config: Config, structs: List[str]) -> List[str]:
    """Filter struct list to small_structs if small flag is set."""
    if config.small:
        return list(filter(lambda s: s in config.small_structs, structs))
    return structs


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
            filename = f"../../timings/{experiment_name}_{self.nproc}-{machine}_{today}{suffix}"
        
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
        # Avoid duplicating the prefix if the line already starts with it
        if line.startswith('./'):
            self.results_file.write(f"{line}\n")
        else:
            self.results_file.write(f"{prefix},{line}\n")
        self.results_file.flush()
    
    def _binary_exists(self, binary: str) -> bool:
        """Check if benchmark binary exists."""
        return os.path.isfile(f"./{binary}")
    
    def _run_single(self, struct: str, impl: str, params: str) -> Tuple[Optional[str], str]:
        """Run a single benchmark and return (result_line, status)."""
        if not self._binary_exists(f"{struct}_{impl}"):
            return None, "missing"
        
        cmd = f"./{struct}_{impl} {params}"
        output, status = run_benchmark(cmd, self.config.numa_cmd, self.config.allocator)
        return get_result_line(output), status
    
    def _log_failure(self, prefix: str, status: str, params: str = ""):
        """Log a failure to the results file."""
        if status == "timeout":
            self.results_file.write(f"# TIMEOUT: {prefix} {params}\n")
        elif status == "crash":
            self.results_file.write(f"# CRASH: {prefix} {params}\n")
        elif status == "error":
            self.results_file.write(f"# ERROR: {prefix} {params}\n")
        self.results_file.flush()
    
    def _run_and_record_binary(self, binary: str, param_str: str,
                                result_markers: tuple = ('mops=', 'GEOMETRIC MEAN')):
        """Run a binary, write matching output lines, log failures.
        Preserves partial results on crash/timeout."""
        prefix = f"./{binary}"
        output, status = run_benchmark(f"{prefix} {param_str}", self.config.numa_cmd, self.config.allocator)
        for line in output.split('\n'):
            line = line.strip()
            if any(m in line for m in result_markers):
                self._write_result(prefix, line)
        if status != "ok":
            self._log_failure(prefix, status, param_str)
        return output, status
    
    def run_geomean(self):
        """Run geometric mean experiment."""
        self.setup_output("geomean")
        self.results_file.write(f"Threads: {self.config.default_p}\n")
        self.results_file.write("---\n")
        
        if self.config.small:
            sizes, list_sizes = [100000, 10000000], [100, 1000]
            updates, trans_vals, zipfians = [0, 5, 50], [4], [0, 0.99]
        else:
            sizes, list_sizes = [10 ** i for i in [5, 6, 7]], [10 ** i for i in [2, 3]]
            updates, trans_vals, zipfians = [0, 5, 50], [1, 4, 16], [0, 0.75, 0.99]
        
        def batch_params(sz_list):
            return {
                'n': ','.join(map(str, sz_list)),
                'u': ','.join(map(str, updates)),
                'z': ','.join(map(str, zipfians)),
                'trans': ','.join(map(str, trans_vals)),
                'p': self.config.default_p,
            }
        
        for impl in self.config.impls:
            for struct in self._all_structs_for_impl(impl):
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue
                
                sz_list = list_sizes if struct in self.config.lstructs else sizes
                print(f"  {struct}_{impl}")
                prefix = f"./{struct}_{impl}"
                
                if impl == "hytm":
                    # HyTM: separate invocations due to memory leak
                    throughputs = []
                    for n in sz_list:
                        for u in updates:
                            for trans in trans_vals:
                                for z in zipfians:
                                    result_line, _ = self._run_and_record(
                                        struct, impl,
                                        {'n': n, 'u': u, 'z': z, 'trans': trans,
                                         'p': self.config.default_p})
                                    mops = parse_mops(result_line) if result_line else None
                                    if mops:
                                        throughputs.append(mops)
                    
                    if throughputs:
                        gmean = geomean(throughputs)
                        self._write_result(prefix, f"GEOMETRIC MEAN OF THROUGHPUTS: {gmean:.4f}")
                else:
                    output, status = self._run_batch(struct, impl, batch_params(sz_list))
        
        self.results_file.close()
    
    def _default_params(self) -> dict:
        """Return the full set of default benchmark parameters."""
        return {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'trans': self.config.default_trans,
            'z': self.config.default_z,
            'p': self.config.default_p,
        }
    
    def _all_structs_for_impl(self, impl: str, override: List[str] = None) -> List[str]:
        """Get all structs (regular + list) to run for an implementation."""
        if override is not None:
            return filter_structs_if_small(self.config, override)
        regular = get_structs_for_impl(self.config, impl)
        return filter_structs_if_small(self.config, regular) + self.config.lstructs
    
    def _make_param_str(self, params: dict) -> str:
        """Build command-line parameter string from a params dict."""
        parts = " ".join(f"-{k} {v}" for k, v in params.items())
        return f"{parts} -r {self.config.rounds} -t {self.config.trial_time}"
    
    def _run_and_record(self, struct: str, impl: str, params: dict) -> Tuple[Optional[str], str]:
        """Run a single benchmark and record its result. Returns (result_line, status)."""
        prefix = f"./{struct}_{impl}"
        param_str = self._make_param_str(params)
        result_line, status = self._run_single(struct, impl, param_str)
        if result_line:
            self._write_result(prefix, result_line)
        if status != "ok":
            self._log_failure(prefix, status, param_str)
        return result_line, status
    
    def _run_batch(self, struct: str, impl: str, params: dict, extra_args: str = "") -> Tuple[str, str]:
        """Run a benchmark with comma-separated param values and record all results.
        Returns (output, status)."""
        param_str = self._make_param_str(params)
        if extra_args:
            param_str += f" {extra_args}"
        return self._run_and_record_binary(f"{struct}_{impl}", param_str)
    
    @staticmethod
    def _supports_batch_mode(impl: str, param_name: str = None) -> bool:
        """Whether the implementation supports comma-separated parameter values."""
        return param_name != 'p'
    
    def _run_varying_experiment(self, name: str, param_name: str, values: List,
                                fixed_params: dict = None, list_values: List = None,
                                structs_override: List[str] = None):
        """Run an experiment that varies one parameter.
        
        Args:
            name: Output file name suffix.
            param_name: Parameter being varied (e.g. 'n', 'p', 'z').
            values: Values to sweep for regular structures.
            fixed_params: Fixed parameters (default: all defaults minus param_name).
            list_values: Alternate sweep values for list structs (default: same
                         values but with n overridden to default_list_n).
            structs_override: Use only these structs, skip list structs.
        """
        if fixed_params is None:
            fixed_params = self._default_params()
            fixed_params.pop(param_name, None)
        
        self.setup_output(name)
        self.results_file.write(f"Fixed: {', '.join(f'{k}={v}' for k, v in fixed_params.items())}\n")
        self.results_file.write("---\n")
        
        for impl in self.config.impls:
            for struct in self._all_structs_for_impl(impl, structs_override):
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue
                print(f"  {struct}_{impl}")
                
                is_list = struct in self.config.lstructs
                sweep = list_values if (is_list and list_values) else values
                if param_name == 'p':
                    sweep = [v for v in sweep if v <= self.nproc]
                
                base = dict(fixed_params)
                if is_list and list_values is None and 'n' in base:
                    base['n'] = self.config.default_list_n
                
                if self._supports_batch_mode(impl, param_name):
                    self._run_batch(struct, impl, {**base, param_name: ','.join(map(str, sweep))})
                else:
                    for val in sweep:
                        self._run_and_record(struct, impl, {**base, param_name: val})
        
        self.results_file.close()
    
    def run_size(self):
        sizes = [int(10 ** i) for i in range(2, 9)]
        list_sizes = [30, 100, 300, 1000]
        self._run_varying_experiment("vary_size", 'n', sizes, list_values=list_sizes)
    
    def run_threads(self):
        if "hytm" in self.config.impls:
            threads = [1, 4, 16, 32, 64]
        else:
            threads = [p for p in [1, 4, 16, 32, 64, 128, 144] if p <= os.cpu_count()]
        self._run_varying_experiment("vary_threads", 'p', threads)
    
    def run_zipfian(self):
        self._run_varying_experiment("vary_zipfian", 'z', [0, 0.5, 0.75, 0.9, 0.99])
    
    def run_trans(self):
        self._run_varying_experiment("vary_trans", 'trans', [1, 2, 4, 8, 16, 32])
    
    def run_update(self):
        self._run_varying_experiment("vary_update", 'u', [0, 5, 10, 20, 50, 100])
    
    def run_range(self):
        fixed = {
            'n': 10 ** 6,
            'u': 100,
            'trans': 4,
            'z': 0,
            'p': self.config.default_p,
            'rqp': 50,
        }
        range_sizes = [int(10 ** i) for i in range(7)]
        self._run_varying_experiment("range_queries", 'rs', range_sizes,
                                    fixed_params=fixed,
                                    structs_override=self.config.range_structs)
    
    def run_tpcc(self):
        """Run TPCC experiments."""
        self.setup_output("tpcc")
        self.results_file.write(f"Threads: {self.config.default_p}\n")
        self.results_file.write("---\n")

        for struct in self.config.tpcc_structs:
            for impl in self.config.tpcc_impls:
                binary = f"tpcc_{struct}_{impl}"
                if not self._binary_exists(binary):
                    continue
                print(f"  {binary}")
                self._run_and_record_binary(
                    binary, f"-nowarmup -p {self.config.default_p}",
                    result_markers=('mops/sec',))

        self.results_file.close()

    def run_nontrivial_types(self):
        """Run non-trivial types experiments.
        
        Part 1: btree inplace vs indirect upserts (100% upserts, vary update rate)
        Part 2: hash_list vs hash_probe across impls (vary update rate)
        """
        self.setup_output("nontrivial_types")
        updates = [0, 5, 10, 20, 50, 100]

        # Part 1: btree inplace vs indirect (100% upserts, vary update rate)
        self.results_file.write("# btree inplace vs indirect (upserts)\n")
        self.results_file.write("---\n")
        base = {
            'n': self.config.default_n,
            'z': self.config.default_z,
            'trans': self.config.default_trans,
            'p': self.config.default_p,
            'u': ','.join(map(str, updates)),
        }
        for struct in ["btree_inplace", "btree"]:
            for impl in ["mvustm"]:
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue
                print(f"  {struct}_{impl}")
                self._run_batch(struct, impl, base, extra_args="-up")

        # Part 2: hash_list, hash_probe, hash_block (vary update rate, otherwise defaults)
        self.results_file.write("# hash_list, hash_probe, hash_block\n")
        self.results_file.write("---\n")
        hash_structs = ["hash_list", "hash_probe", "hash_block"]
        for impl in ["mvustm"]:
            for struct in hash_structs:
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue
                print(f"  {struct}_{impl}")
                params = self._default_params()
                params.pop('u')
                self._run_batch(struct, impl, {**params, 'u': ','.join(map(str, updates))})

        self.results_file.close()

    def run_ablation(self):
        """Run ablation studies on btree.
        
        Part 1: Timestamping variants (hwstamp, split, lazy, eager) — vary update rate
        Part 2: Abort mechanism (standard, longjmp, simple_longjmp, non-MV) — vary update rate
        """
        self.setup_output("ablation")
        updates = [0, 5, 10, 20, 50, 100]

        base = {
            'n': self.config.default_n,
            'z': self.config.default_z,
            'trans': self.config.default_trans,
            'p': self.config.default_p,
            'u': ','.join(map(str, updates)),
        }

        # Part 1: Timestamping variants
        self.results_file.write("# Timestamping: split (default), hwstamp, lazy, eager, fuse hwstamp, multiverse hwstamp\n")
        self.results_file.write("---\n")
        ts_impls = ["mvustm", "mvustm_hwstamp", "mvustm_lzstamp", "mvustm_eager",
                    "mvustm_agg_funnel_eager", "fuse", "fuse_hwstamp"]
        for impl in ts_impls:
            if not self._binary_exists(f"btree_{impl}"):
                continue
            print(f"  btree_{impl}")
            self._run_batch("btree", impl, base)

        # Part 2: Abort mechanism (non-MV)
        self.results_file.write("# Abort mechanism: standard, longjmp, simple_longjmp\n")
        self.results_file.write("---\n")
        abort_impls = ["ustm", "ustm_longjmp", "ustm_simple_longjmp"]
        for impl in abort_impls:
            if not self._binary_exists(f"btree_{impl}"):
                continue
            print(f"  btree_{impl}")
            self._run_batch("btree", impl, base)

        self.results_file.close()

    def run_all(self):
        """Run all experiments."""
        print("=" * 50)
        print("Running all MVUSTM experiments")
        print("=" * 50)
        
        experiments = [
            # ("Experiment 8: TPCC", self.run_tpcc),
            ("Experiment 1: Geometric Mean", self.run_geomean),
            ("Experiment 2: Varying Size", self.run_size),
            ("Experiment 3: Varying Threads", self.run_threads),
            ("Experiment 4: Varying Zipfian", self.run_zipfian),
            ("Experiment 5: Varying Transaction Size", self.run_trans),
            ("Experiment 6: Varying Update Rate", self.run_update),
            ("Experiment 7: Range Queries", self.run_range),
            ("Experiment 9: Non-trivial Types", self.run_nontrivial_types),
            ("Experiment 10: Ablation Study", self.run_ablation),
        ]
        
        for name, func in experiments:
            print(f"\n=== {name} ===")
            start_time = time.time()
            func()
            elapsed = time.time() - start_time
            print(f"Completed in {elapsed:.1f}s ({elapsed/60:.1f}m)")
        
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
  tpcc       TPCC benchmark
  nontrivial Non-trivial types (btree upsert variants, hash_list/hash_probe)
  ablation   Ablation studies (timestamping, abort mechanism)
  all        Run all experiments

Examples:
  ./run_experiment.py geomean
  ./run_experiment.py all --hytm
  ./run_experiment.py threads --output ./results
""")
    
    parser.add_argument("experiment", 
                       choices=["geomean", "size", "threads", "zipfian", "trans", "update", "range", "tpcc", "nontrivial", "ablation", "all"],
                       help="Experiment to run")
    parser.add_argument("--hytm", action="store_true",
                       help="Include HyTM (limits to 72 threads on 2 sockets)")
    parser.add_argument("--output", "-o", metavar="DIR",
                       help="Output directory for results")
    parser.add_argument("--small", action="store_true",
                       help="Restrict to btree, hash_block, and list (geomean uses runall.sh parameters)")
    parser.add_argument("--allocator", default="deqalloc",
                       help="Choose which allocator to use (default: deqalloc)")
    parser.add_argument("--impl", default=None,
                       choices=["mvustm", "fuse", "2plsf", "multiverse"],
                       help="Run a specific stm system only")
    parser.add_argument("--struct", default=None,
                       choices=["btree", "arttree", "avltree", "leaftree", "treap", "skiplist", "hash_block"],
                       help="Run a specific struct only")
    
    args = parser.parse_args()
    
    #make sure we are in the build directory
    benchmark_dir=subprocess.getoutput("realpath "+re.sub(os.path.basename(__file__), "", os.path.realpath(__file__)))+"/"
    build_dir = benchmark_dir + "../build/benchmark"
    curr_dir = os.getcwd()
    os.chdir(build_dir) #pushd

    config = Config(args)
    runner = ExperimentRunner(config, args.output)

    experiments = {
        "geomean": runner.run_geomean,
        "size": runner.run_size,
        "threads": runner.run_threads,
        "zipfian": runner.run_zipfian,
        "trans": runner.run_trans,
        "update": runner.run_update,
        "range": runner.run_range,
        "tpcc": runner.run_tpcc,
        "nontrivial": runner.run_nontrivial_types,
        "ablation": runner.run_ablation,
        "all": runner.run_all,
    }
    
    start_time = time.time()
    experiments[args.experiment]()
    elapsed = time.time() - start_time
    
    if args.experiment != "all":  # run_all() prints its own timing
        print(f"\nCompleted in {elapsed:.1f}s ({elapsed/60:.1f}m)")

    os.chdir(curr_dir) #popd

if __name__ == "__main__":
    main()
