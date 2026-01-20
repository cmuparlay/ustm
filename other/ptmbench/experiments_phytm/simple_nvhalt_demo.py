#!/usr/bin/python3
"""
Simplified NV-HALT experiment example demonstrating usage with NVRAM disabled
"""
from _basic_functions import *
import copy

# Configuration for running NV-HALT with NVRAM disabled
unfiltered_algs = dict({
      'abtree.none.phytm1_fgl_colocated_locks'                    : dict(name='NV-HALT-NVRAM'     , use=True)
    , 'abtree.none.phytm1_fgl_colocated_locks_no_pmem'           : dict(name='NV-HALT-NO-NVRAM'  , use=True)  
    , 'abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence'  : dict(name='NV-HALT-NO-FLUSH'  , use=True)
    , 'abtree.none.phytm1_fgl_colocated_locks_no_phtm'          : dict(name='NV-HALT-VOLATILE'   , use=True)
})

filtered_algs = dict()
for k,v in unfiltered_algs.items():
    if k and v and v['name'] and v['use']:
        filtered_algs[k] = v['name']

def extract_filtered_algs(exp_dict, file_name, field_name):
    result = grep_line(exp_dict, file_name, 'algorithm')
    if result in filtered_algs.keys():
        return filtered_algs[result]
    return ''

def define_experiment(exp_dict, args):
    set_dir_compile  (exp_dict, os.getcwd() + '/../')
    set_dir_tools    (exp_dict, os.getcwd() + '/../setbench/tools')
    set_dir_run      (exp_dict, os.getcwd() + '/../bin')
    set_dir_data     (exp_dict, os.getcwd() + '/data')

    # Add algorithms to test
    add_run_param    (exp_dict, 'algorithm'         , list(filtered_algs.keys()))
    
    # Workload parameters  
    add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['5.0 5.0'])    # 5% ins, 5% del, 90% find
    add_run_param    (exp_dict, 'MAXKEY'            , [100000])       # Key range
    add_run_param    (exp_dict, 'millis'            , [5000])         # Run time
    add_run_param    (exp_dict, 'WORK_THREADS'      , [1, 2, 4])      # Thread counts
    add_run_param    (exp_dict, '__trials'          , [1, 2, 3])      # Number of trials

    # Thread pinning
    add_run_param    (exp_dict, 'thread_pinning'    , ['-pin ' + shell_to_str('cd ' + get_dir_tools(exp_dict) + ' ; ./get_pinning_cluster.sh', exit_on_error=True)])

    # Run command
    cmd_run = 'timeout 60 numactl --interleave=all ./{algorithm} -nwork {WORK_THREADS} -nprefill {WORK_THREADS} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'
    
    set_cmd_compile  (exp_dict, '../compile.sh')
    set_cmd_run      (exp_dict, cmd_run)

    # Data fields to capture
    add_data_field   (exp_dict, 'alg'               , coltype='TEXT', extractor=extract_filtered_algs)
    add_data_field   (exp_dict, 'total_throughput'  , coltype='INTEGER', validator=is_positive)
    add_data_field   (exp_dict, 'TOTAL_THREADS'     , coltype='INTEGER')
    add_data_field   (exp_dict, 'elapsed_millis'    , coltype='INTEGER', validator=is_positive)