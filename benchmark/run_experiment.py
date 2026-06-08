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
    --output DIR  Output directory for results
    --small       Restrict to btree, hash_list, and list (geomean uses runall.sh parameters)

Implementations tested:
    mvustm, mvustm_hwstamp, mvustm_longjmp, 2plsf, fuse, multiverse, onfile, tinystm, tl2

Examples:
    ./run_experiment.py geomean
    ./run_experiment.py all
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
                "onefile",
                "tiny",
                "tl2",
                # Blocking optimistic-locking baseline (other/fuse/include/structures,
                # no Versioned/LazyStamp/FlockHelp): the "fine-grained concurrent"
                # counterpart to the STM impls. Point-update only — its binary
                # silently skips trans>1 runs.
                "ol",
                # DCOTL is broken for most benchmarks
                # "dcotl"
            ]
            self.tpcc_impls = ["mvustm", "fuse", "2plsf", "multiverse"]
            self.impls_explicit = False
        else:
            #only run specified impls
            self.impls = self.tpcc_impls = list(args.impl)
            self.impls_explicit = True

        self.max_threads = os.cpu_count()
        self.numa_cmd = "numactl -i all"
        self.default_p = self.max_threads
        self.allocator = args.allocator

        if args.struct == None:
            self.structs = ["btree", "arttree", "avltree", "leaftree", "treap", "skiplist", "hash_list"]
            self.lstructs = ["list"]
            self.range_structs = ["btree", "arttree"]
            self.small_structs = ["btree", "hash_list"]
            self.tpcc_structs = ["hash_list", "btree", "mix"]
        else:
            self.structs = self.range_structs = self.small_structs = self.tpcc_structs = list(args.struct)
            self.lstructs = ["list"] if "list" in args.struct else []
        print(self)

    """Experiment configuration."""
    # Data structures
    structs: List[str] = None
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

    allocator: str = "jemalloc"


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

def get_structs_for_impl(config: Config, impl: str) -> List[str]:
    """Get structures compatible with implementation."""
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
        if self.output_dir:
            os.makedirs(self.output_dir, exist_ok=True)
            filename = os.path.join(self.output_dir, experiment_name)
        else:
            filename = f"../../timings/{experiment_name}_{self.nproc}-{machine}_{today}"
        
        print(f"Output: {filename}")
        print(f"max_threads={self.config.max_threads}")
        
        self.results_file = open(filename, 'w')
        self._write_header(experiment_name, today)
        return filename
    
    def _write_header(self, experiment_name: str, today: str):
        """Write header to results file."""
        self.results_file.write(f"Date: {today}\n")
        self.results_file.write(f"Experiment: {experiment_name}\n")
        self.results_file.write(f"Threads: {self.config.max_threads}\n")
        
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
                                structs_override: List[str] = None,
                                impls_override: List[str] = None):
        """Run an experiment that varies one parameter.

        Args:
            name: Output file name suffix.
            param_name: Parameter being varied (e.g. 'n', 'p', 'z').
            values: Values to sweep for regular structures.
            fixed_params: Fixed parameters (default: all defaults minus param_name).
            list_values: Alternate sweep values for list structs (default: same
                         values but with n overridden to default_list_n).
            structs_override: Use only these structs, skip list structs.
            impls_override: Use this list of impls instead of self.config.impls.
                            For experiments that target a focused set of arms
                            (e.g. the OL TLF variants).
        """
        if fixed_params is None:
            fixed_params = self._default_params()
            fixed_params.pop(param_name, None)

        self.setup_output(name)
        self.results_file.write(f"Fixed: {', '.join(f'{k}={v}' for k, v in fixed_params.items())}\n")
        self.results_file.write("---\n")

        impls = impls_override if impls_override is not None else self.config.impls
        for impl in impls:
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
        threads = [p for p in [1, 4, 16, 32, 64, 128] if p <= os.cpu_count()]
        threads.append(os.cpu_count())
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

        # Part 2: hash_list, hash_probe, hash_list (vary update rate, otherwise defaults)
        self.results_file.write("# hash_list, hash_probe, hash_list\n")
        self.results_file.write("---\n")
        hash_structs = ["hash_probe", "hash_list"]
        for struct in hash_structs:
            impls = ["mvustm", "mvustm_indirect"] if struct == "hash_probe" else ["mvustm"]
            for impl in impls:
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

    def _run_experiment_list(self, title: str, experiments: List[Tuple[str, callable]]):
        print("=" * 50)
        print(title)
        print("=" * 50)

        for name, func in experiments:
            print(f"\n=== {name} ===")
            start_time = time.time()
            func()
            elapsed = time.time() - start_time
            print(f"Completed in {elapsed:.1f}s ({elapsed/60:.1f}m)")

        print("\n" + "=" * 50)
        print(f"{title} complete!")
        print("=" * 50)

    def run_stm_vs_ol(self):
        """STM vs fine-grained-concurrent (blocking OL) geomean comparison.

        Mirrors run_geomean's methodology — same size/update/zipfian sweep at
        the default thread count — but pinned to trans=1 because OL is
        point-update only. The C++ binary emits one geomean per impl/struct
        pair across the parameter sweep.
        """
        self.setup_output("stm_vs_ol")
        self.results_file.write(f"Threads: {self.config.default_p}\n")
        self.results_file.write("---\n")

        if self.config.small:
            sizes, list_sizes = [100000, 10000000], [100, 1000]
            updates, zipfians = [0, 5, 50], [0, 0.99]
        else:
            sizes, list_sizes = [10 ** i for i in [5, 6, 7]], [10 ** i for i in [2, 3]]
            updates, zipfians = [0, 5, 50], [0, 0.75, 0.99]

        def batch_params(sz_list):
            return {
                'n': ','.join(map(str, sz_list)),
                'u': ','.join(map(str, updates)),
                'z': ','.join(map(str, zipfians)),
                'trans': 1,
                'p': self.config.default_p,
            }

        # stm_vs_ol scopes to the four STM systems we benchmark against
        # the blocking-OL baseline: uSTM (SoftStamp), Fuse (SoftStamp),
        # 2PLSF, Multiverse. No HWStamp variants — the bar chart only
        # compares the SoftStamp implementations of each system head-
        # to-head against ol. Other STM impls (onefile/tiny/tl2/hytm)
        # are excluded. Honour --impl if the user passed it explicitly.
        # _binary_exists() filters out impls that weren't built.
        default_stm_impls = ["mvustm", "fuse", "2plsf", "multiverse"]
        if self.config.impls_explicit:
            impls = list(self.config.impls)
        else:
            impls = default_stm_impls + ["ol"]

        for impl in impls:
            structs_for_impl = self._all_structs_for_impl(impl)
            # The OL family doesn't have hash_list, substitute hash_block.
            # For STM impls, include BOTH hash_list (linked-list buckets)
            # and hash_block (block buckets) so the OL hash_block has a
            # same-algorithm STM counterpart, not just a cross-algorithm
            # comparison.
            if impl == "ol":
                structs_for_impl = ["hash_block" if s == "hash_list" else s
                                    for s in structs_for_impl]
            elif "hash_list" in structs_for_impl and "hash_block" not in structs_for_impl:
                structs_for_impl = structs_for_impl + ["hash_block"]
            for struct in structs_for_impl:
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue

                sz_list = list_sizes if struct in self.config.lstructs else sizes
                print(f"  {struct}_{impl}")
                self._run_batch(struct, impl, batch_params(sz_list))

        self.results_file.close()

    def run_txn_overhead(self):
        """Transactional-wrapping overhead on OL data structures.

        Same plain blocking OL structures in three arms — only the
        stm::atomic_region wrapper and lock-fusion mechanism differ:
          - ol             : pass-through (identity). 4 ops run in
                             a loop unwrapped.
          - ol_ustm_counter: ustm::transaction with the TLF counter
                             protocol — OL locks bump inProtected
                             instead of acquiring, reads in unprotected
                             regions skip readLog. Atomicity via uSTM's
                             commit-time write locks.
          - ol_fuse        : fuse::atomic_region with fuse's native TLF
                             implementation — flck::lock::try_lock is
                             intercepted directly, logged into the txn
                             descriptor, taken at commit. The canonical
                             SPAA'25 published implementation, included
                             as a sanity check.

        Trans is pinned at 4 (the experiment's whole point); size,
        update rate, and zipfian sweep exactly like run_geomean /
        run_stm_vs_ol. Per impl/struct: 27 configs → one geomean.

        Only meaningful with OL structures — wrapping the corresponding
        sequential containers in a retryable transaction would crash.
        """
        self.setup_output("txn_overhead")
        self.results_file.write(f"Threads: {self.config.default_p}\n")
        self.results_file.write("---\n")

        if self.config.small:
            sizes, list_sizes = [100000, 10000000], [100, 1000]
            updates, zipfians = [0, 5, 50], [0, 0.99]
        else:
            sizes, list_sizes = [10 ** i for i in [5, 6, 7]], [10 ** i for i in [2, 3]]
            updates, zipfians = [0, 5, 50], [0, 0.75, 0.99]

        def batch_params(sz_list):
            return {
                'n': ','.join(map(str, sz_list)),
                'u': ','.join(map(str, updates)),
                'z': ','.join(map(str, zipfians)),
                'trans': 4,  # PINNED — the point of the experiment
                'p': self.config.default_p,
            }

        # OL has hash_block where STM has hash_list; rename so the
        # _all_structs_for_impl helper still gives us the right list.
        ol_structs = ["btree", "arttree", "avltree", "leaftree",
                      "treap", "skiplist", "hash_block"]

        for impl in ("ol",
                     "ol_ustm_counter", "ol_ustm_counter_hwstamp",
                     "ol_ustm_full",    "ol_ustm_full_hwstamp",
                     "ol_fuse",         "ol_fuse_hwstamp",
                     "ol_fuse_stm",     "ol_fuse_stm_hwstamp"):
            for struct in ol_structs + self.config.lstructs:
                if not self._binary_exists(f"{struct}_{impl}"):
                    continue
                sz_list = list_sizes if struct in self.config.lstructs else sizes
                print(f"  {struct}_{impl}")
                self._run_batch(struct, impl, batch_params(sz_list))

        self.results_file.close()

    # ---- OL TLF parameter-varying experiments ---------------------------
    def _ol_tlf_impls(self):
        """Standard set of arms for the OL TLF varying experiments.

        Mixes OL+wrapper variants (ol_*) with one cross-system arm
        (2plsf, STM-native) — fuse doesn't expose a "no-TLF" 2PLSF
        path for OL structures, so the cross-system version is what
        fuse's own paper compares against. ol_multiverse is the
        OL+multiverse wrap (multiverse_glue's stm::lock is a no-op,
        which is semantically fine inside an updateTx — multiverse's
        tmtype tracking handles atomicity in place of flck::lock).
        """
        return ("ol",
                "ol_ustm_counter", "ol_ustm_counter_hwstamp",
                "ol_ustm_full",    "ol_ustm_full_hwstamp",
                "ol_fuse",         "ol_fuse_hwstamp",
                "ol_fuse_stm",     "ol_fuse_stm_hwstamp",
                "2plsf",
                "ol_multiverse")

    def _ol_tlf_structs(self):
        """Structures with all _ol* variants (omits hash_list — STM-only)."""
        return ["btree", "arttree", "avltree", "leaftree",
                "treap", "skiplist", "hash_block"]

    def run_vary_txn_trans(self):
        """Sweep transaction size for the OL TLF arms.

        Directly tests whether STM overhead amortizes with trans size as
        the SPAA'25 paper predicts. n, u, z fixed at defaults; only trans
        varies. p = default (max threads). For each (impl, struct) the
        binary internally geomeans across the trans values but we also
        capture per-config rows for line plots.
        """
        trans_vals = [1, 2, 4, 8, 16, 32]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'z': self.config.default_z,
            'p': self.config.default_p,
        }
        self._run_varying_experiment(
            "vary_txn_trans", 'trans', trans_vals,
            fixed_params=fixed,
            structs_override=self._ol_tlf_structs() + self.config.lstructs,
            impls_override=list(self._ol_tlf_impls()),
        )

    def run_stm_vs_ol_vary_zipfian(self):
        """Sweep zipfian for the stm_vs_ol arms.

        Same 5-system set as run_stm_vs_ol — the 4 STM systems
        (SoftStamp only — no HWStamp variants) plus bare OL. trans
        pinned at 1 (OL is point-update only). n, u, p fixed at
        defaults; only z varies.
        """
        z_vals = [0, 0.5, 0.75, 0.9, 0.99]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'trans': 1,
            'p': self.config.default_p,
        }
        stm_vs_ol_impls = ["mvustm", "fuse", "2plsf", "multiverse", "ol"]
        # Focused subset for the paper figure: a tree, a balanced tree,
        # and a hash. Matches the structure set used by
        # run_txn_overhead_vary_trans.
        structs = ["btree", "avltree", "hash_block"]
        self._run_varying_experiment(
            "stm_vs_ol_vary_z", 'z', z_vals,
            fixed_params=fixed,
            structs_override=structs,
            impls_override=stm_vs_ol_impls,
        )

    def run_stm_vs_ol_vary_trans(self):
        """Sweep trans for the stm_vs_ol arms on a focused subset.

        Same 5-system set as run_stm_vs_ol_vary_zipfian — SoftStamp
        STMs only, no HWStamp variants. z, n, u, p fixed at defaults;
        only trans varies. OL silently skips trans>1 — its line
        collapses to a single point at trans=1.
        """
        trans_vals = [1, 2, 4, 8, 16, 32]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'z': self.config.default_z,
            'p': self.config.default_p,
        }
        stm_vs_ol_impls = ["mvustm", "fuse", "2plsf", "multiverse", "ol"]
        structs = ["btree", "avltree", "hash_block"]
        self._run_varying_experiment(
            "stm_vs_ol_vary_trans", 'trans', trans_vals,
            fixed_params=fixed,
            structs_override=structs,
            impls_override=stm_vs_ol_impls,
        )

    def run_txn_overhead_vary_trans(self):
        """Vary trans on a focused subset of structures.

        Three structures (btree, avltree, hash_block), n = 10^6, z = 0,
        every other parameter at its default. Produces tight per-struct
        line plots suitable for a single paper figure row. Impl set is
        the 5 systems the paper figure compares: bare OL, 2PLSF
        (cross-system, STM-native), Fuse-TLF (SoftStamp), Multiverse,
        and uSTM (no TLF, SoftStamp). Other ol_* ablation arms are not
        shown in the figure, so we don't run them — the broader
        _ol_tlf_impls() helper is reserved for the exploratory
        vary_txn_trans / vary_txn_threads experiments.
        """
        trans_vals = [1, 2, 4, 8, 16, 32]
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'z': 0,
            'p': self.config.default_p,
        }
        impls = ["ol", "2plsf", "ol_fuse", "ol_multiverse", "ol_ustm_full"]
        self._run_varying_experiment(
            "txn_overhead_vary_trans", 'trans', trans_vals,
            fixed_params=fixed,
            structs_override=["btree", "avltree", "hash_block"],
            impls_override=impls,
        )

    # Three arms shared by every strings_* experiment, single structure.
    # Both string arms run the same binary; -bigval is a runtime flag
    # (added in commit ed60d1b). To distinguish the two in the output
    # file we rewrite the binary's `./hash_probe_string_mvustm` prefix
    # to `./hash_probe_string_bigval_mvustm` for the bigval run so
    # plot.py's parser splits the impl correctly.
    _STRINGS_CONFIGS = [
        # (binary,                        extra_flag, recorded_impl)
        ("hash_probe_mvustm",             "",         "mvustm"),
        ("hash_probe_string_mvustm",      "",         "string_mvustm"),
        ("hash_probe_string_mvustm",      "-bigval",  "string_bigval_mvustm"),
    ]

    def _run_strings_sweep(self, name, sweep_param, sweep_vals, fixed_params):
        """Driver shared by strings_update and strings_zipfian.

        Sweeps `sweep_param` across `sweep_vals` on hash_probe with the
        three value-size arms (Integer, Short String, Long String).
        `fixed_params` is a dict of the params held constant.
        """
        self.setup_output(name)
        self.results_file.write(
            "Fixed: " +
            ", ".join(f"{k}={v}" for k, v in fixed_params.items()) +
            "\n")
        self.results_file.write("---\n")

        for binary, extra, recorded_impl in self._STRINGS_CONFIGS:
            if not self._binary_exists(binary):
                print(f"  SKIP hash_probe_{recorded_impl} ({binary} not built)")
                continue
            print(f"  hash_probe_{recorded_impl}")
            record_prefix = f"./hash_probe_{recorded_impl}"
            for val in sweep_vals:
                fixed_str = " ".join(f"-{k} {v}" for k, v in fixed_params.items())
                param_str = (
                    f"{fixed_str} "
                    f"-{sweep_param} {val} "
                    f"-r 1 -t 1 {extra}"
                ).strip()
                output, status = run_benchmark(
                    f"./{binary} {param_str}",
                    self.config.numa_cmd,
                    self.config.allocator)
                for line in output.split('\n'):
                    line = line.strip()
                    if 'mops=' in line or 'GEOMETRIC MEAN' in line:
                        # Substitute the binary's emitted prefix with
                        # our recorded prefix so the two -bigval arms
                        # remain distinguishable post-parse.
                        if line.startswith(f"./{binary},"):
                            line = record_prefix + line[len(f"./{binary}"):]
                        self.results_file.write(f"{line}\n")
                        self.results_file.flush()
                if status != "ok":
                    self._log_failure(record_prefix, status, param_str)

        self.results_file.close()

    def run_strings_update(self):
        """Vary update rate on hash_probe + mvustm, three value-size variants.

        Integer (long V), Short String (~1-char V + dense small-int
        keys), Long String (-bigval: ~20-char V + hashed 64-bit keys).
        All other parameters at defaults.
        """
        self._run_strings_sweep(
            "strings_update", "u", [0, 5, 10, 20, 50, 100],
            {
                "n":     self.config.default_n,
                "trans": self.config.default_trans,
                "z":     self.config.default_z,
                "p":     self.config.default_p,
            })

    def run_strings_zipfian(self):
        """Same three arms as run_strings_update, but vary zipfian.

        Holds update rate at its default; sweeps z across the standard
        zipfian set. Useful for showing how skew interacts with the
        string-value path (cache-resident hot keys vs uniform access).
        """
        self._run_strings_sweep(
            "strings_zipfian", "z", [0, 0.5, 0.75, 0.9, 0.99],
            {
                "n":     self.config.default_n,
                "trans": self.config.default_trans,
                "u":     self.config.default_u,
                "p":     self.config.default_p,
            })

    def run_vary_txn_threads(self):
        """Sweep thread count for the OL TLF arms, trans pinned at 4.

        Tests where commit-stamp contention (HWStamp vs SplitStamp) and
        general STM scaling kicks in. At p=1 STM overhead should be
        roughly fixed per-op; at p=144 commit-stamp CAS contention
        should dominate the non-HWStamp variants.
        """
        threads = [p for p in [1, 4, 16, 32, 64, 128] if p <= self.nproc]
        if self.nproc not in threads:
            threads.append(self.nproc)
        fixed = {
            'n': self.config.default_n,
            'u': self.config.default_u,
            'z': self.config.default_z,
            'trans': 4,
        }
        self._run_varying_experiment(
            "vary_txn_threads", 'p', threads,
            fixed_params=fixed,
            structs_override=self._ol_tlf_structs() + self.config.lstructs,
            impls_override=list(self._ol_tlf_impls()),
        )

    def run_sweep(self):
        """Run parameter sweep experiments (2-6)."""
        self._run_experiment_list("Running parameter sweep experiments", [
            ("Experiment 2: Varying Size", self.run_size),
            ("Experiment 3: Varying Threads", self.run_threads),
            ("Experiment 4: Varying Zipfian", self.run_zipfian),
            ("Experiment 5: Varying Transaction Size", self.run_trans),
            ("Experiment 6: Varying Update Rate", self.run_update),
        ])

    def run_all(self):
        """Run all experiments."""
        self._run_experiment_list("Running all MVUSTM experiments", [
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
        ])


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
  stm_vs_ol  STM vs fine-grained-concurrent (blocking OL) at trans=1
  stm_vs_ol_vary_z  Sweep zipfian for the stm_vs_ol arms (trans=1, rest default)
  stm_vs_ol_vary_trans  Sweep trans for the stm_vs_ol arms (z=default, rest default)
  txn_overhead   OL vs OL+fuse-txn-wrap on same OL structures at trans=4
  vary_txn_trans   Sweep trans size for OL TLF arms (tests amortization claim)
  txn_overhead_vary_trans Focused trans sweep (btree/avltree/hash_block, n=10^7, z=0)
  strings_update Sweep update rate on hash_probe + mvustm: long vs string vs string-bigval
  strings_zipfian Same three arms but sweep zipfian instead of update rate
  vary_txn_threads Sweep thread count for OL TLF arms (tests stamp contention)
  sweep      Run parameter sweep experiments (size, threads, zipfian, trans, update)
  all        Run all experiments

Examples:
  ./run_experiment.py geomean
  ./run_experiment.py all
  ./run_experiment.py threads --output ./results
""")
    
    parser.add_argument("experiment", nargs="+",
                       choices=["geomean", "size", "threads", "zipfian", "trans", "update", "range", "tpcc", "nontrivial", "ablation", "stm_vs_ol", "stm_vs_ol_vary_z", "stm_vs_ol_vary_trans", "txn_overhead", "vary_txn_trans", "txn_overhead_vary_trans", "vary_txn_threads", "strings_update", "strings_zipfian", "sweep", "all"],
                       help="Experiment(s) to run (one or more)")
    parser.add_argument("--output", "-o", metavar="DIR",
                       help="Output directory for results")
    parser.add_argument("--small", action="store_true",
                       help="Restrict to btree, hash_list, and list (geomean uses runall.sh parameters)")
    parser.add_argument("--allocator", default="jemalloc",
                       help="Choose which allocator to use (default: deqalloc)")
    parser.add_argument("--impl", default=None, nargs="+",
                       choices=["mvustm", "mvustm_hwstamp",
                                "fuse", "fuse_hwstamp",
                                "2plsf", "multiverse", "onefile", "tiny", "tl2", "ol",
                                "ol_ustm_counter", "ol_ustm_counter_hwstamp",
                                "ol_ustm_full", "ol_ustm_full_hwstamp",
                                "ol_fuse", "ol_fuse_hwstamp",
                                "ol_fuse_stm", "ol_fuse_stm_hwstamp"],
                       help="Run only the specified stm system(s)")
    parser.add_argument("--struct", default=None, nargs="+",
                       choices=["btree", "arttree", "avltree", "leaftree", "treap", "skiplist", "hash_list"],
                       help="Run specific struct(s) only")
    
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
        "stm_vs_ol": runner.run_stm_vs_ol,
        "stm_vs_ol_vary_z": runner.run_stm_vs_ol_vary_zipfian,
        "stm_vs_ol_vary_trans": runner.run_stm_vs_ol_vary_trans,
        "strings_update": runner.run_strings_update,
        "strings_zipfian": runner.run_strings_zipfian,
        "txn_overhead": runner.run_txn_overhead,
        "vary_txn_trans": runner.run_vary_txn_trans,
        "txn_overhead_vary_trans": runner.run_txn_overhead_vary_trans,
        "vary_txn_threads": runner.run_vary_txn_threads,
        "sweep": runner.run_sweep,
        "all": runner.run_all,
    }
    
    if len(args.experiment) == 1:
        exp = args.experiment[0]
        start_time = time.time()
        experiments[exp]()
        elapsed = time.time() - start_time
        if exp not in ("all", "sweep"):  # these print their own timing
            print(f"\nCompleted in {elapsed:.1f}s ({elapsed/60:.1f}m)")
    else:
        runner._run_experiment_list(
            f"Running {len(args.experiment)} experiments: {', '.join(args.experiment)}",
            [(name, experiments[name]) for name in args.experiment],
        )

    os.chdir(curr_dir) #popd

if __name__ == "__main__":
    main()
