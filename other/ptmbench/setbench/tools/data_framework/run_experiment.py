#!/usr/bin/python3

##
## Possible improvements
##
## - some mechanism for passing token "replacement" information to user_plot_style.py etc...
## - plotting sanity checks: optional fields for user predictions in plot preparation? %min/max range threshold for warning? predicted rows per data point?
## - break relative path dependency on tools
##
## - line plots with proper markers and styling
##
## - "clean" version of example without comments to show that it's still clear, but also way more concise...
##
## - animation support? (proper matplotlib animations???) (fractional row plotting with desired frame rate + count? need to specify a time axis...)
##
## - ability to link run_params so it's not just the plain cross product of all run_params that gets explored... (or perhaps a user-specified predicate that tests and rejects combinations of run_params)
##
## More general improvements (mostly to setbench -- written here because I'm lazy, and these are just moving to follow my attention/gaze...)
##
## - advplot into tools
## - equivalent of this for macrobench?
##
## - stress test scripts
## - leak checking scripts
## - flamegr script
## - dashboard for performance comparison of DS
##
## - experiment example for comparison of reclamation algs
## - memhook integration with example experiments
## - worked example of DS perf comparison understanding from basic cycles -> instr/cachemiss -> location w/perfrecord -> memhook layout causation
##      temporal profiling with perf -> temporal flamegr of measured phase
##

import inspect
import subprocess
import os
import sys
import datetime
import argparse
import sqlite3
import numpy
import io
import re
import glob
import multiprocessing
import types
from colorama import Fore, Back, Style

import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import numpy as np
import seaborn as sns
from IPython.display import *

from _templates_html import *
from _basic_functions import *
from _plot_df import *

######################################################
#### global variables
######################################################

## config

MULTITHREAD_PLOT = True
MULTITHREAD_CREATEDB = True

## originally from _basic_functions.py
g = None
pp_log = None
pp_stdout = None

## originally from this file
__rerunstep = 0
step = 0
maxstep = 0
started_sec = 0
started = 0
con = None
cur = None
db_path = None
validation_errors = None
filename_to_html = None             ## used by all html building functions below
filenames_in_navigation = None      ## used by all html building functions below

DISABLE_TEE_STDOUT = False
DISABLE_LOGFILE_CLOSE = False

def _shell_to_str_private(cmd, exit_on_error=False):
    child = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    returncode = child.returncode
    if returncode != 0:
        print()
        print(Back.RED+Fore.BLACK+'########################################################'+Style.RESET_ALL)
        print(Back.RED+Fore.BLACK+'#### ERROR OUTPUT:'+Style.RESET_ALL)
        print(Back.RED+Fore.BLACK+'########################################################'+Style.RESET_ALL)
        if child.stderr != None:
            ret_stderr = child.stderr.decode('utf-8').rstrip(' \r\n')
            print()
            print(ret_stderr)
            print()
            if exit_on_error:
                print(child.stdout.decode('utf-8').rstrip(' \r\n'))
                print()
        print(Back.RED+Fore.BLACK+'ERROR: running "{}" ; returncode {}'.format(cmd, returncode)+Style.RESET_ALL)
        if exit_on_error: do_exit()
    ret_stdout = child.stdout.decode('utf-8').rstrip(' \r\n')
    return ret_stdout

def do_init_basic_functions():
    global g
    global pp_log
    global pp_stdout

    global step
    global maxstep
    global started_sec
    global started
    global con
    global cur
    global db_path
    global validation_errors
    global filename_to_html
    global filenames_in_navigation

    g = dict()
    g['run_params'] = dict()
    g['run_params_filters'] = dict()
    g['data_fields'] = dict()
    g['replacements'] = dict()
    g['data_file_paths'] = []
    g['plots'] = []
    g['pages'] = []
    g['log'] = open('output_log.txt', 'w')
    g['sanity_check_failures'] = []
    g['replacements']['__hostname'] = _shell_to_str_private('hostname')
    pp_log = pprint.PrettyPrinter(indent=4, stream=g['log'])
    pp_stdout = pprint.PrettyPrinter(indent=4)

    step = 0
    maxstep = 0
    started_sec = 0
    started = 0
    con = None
    cur = None
    db_path = None
    validation_errors = []
    filename_to_html = dict()
    filenames_in_navigation = list()

######################################################
#### private helper functions
######################################################

def do_raise():
    g['log'].flush()
    g['log'].close()
    raise

def do_exit(code=1, _do_raise=False):
    g['log'].flush()
    g['log'].close()
    if _do_raise: do_raise() #sys.exit(code)

def replace(str):
    return str.format(**g['replacements'])

def replace_and_run(str, exit_on_error=False):
    return shell_to_str(replace(str), exit_on_error)

def rshell_to_str(str, exit_on_error=False):
    return shell_to_str(replace(str), exit_on_error)

def rshell_to_list(str, exit_on_error=False):
    return shell_to_list(replace(str), exit_on_error)

def log(str='', _end='\n', exp_dict=None):
    if not exp_dict: exp_dict = g
    if exp_dict: print('{}'.format(str), file=exp_dict['log'], end=_end)

def tee(str='', _end='\n'):
    if DISABLE_TEE_STDOUT == False: print('{}'.format(str), end=_end)
    if g: print('{}'.format(str), file=g['log'], end=_end)

def log_replace_and_run(str, exit_on_error=False):
    log(replace_and_run(str, exit_on_error), _end='')

def tee_replace_and_run(str, exit_on_error=False):
    tee(replace_and_run(str, exit_on_error), _end='')

def pp(obj):
    pp_stdout.pprint(obj)

def log_pp(obj):
    pp_log.pprint(obj)

def tee_pp(obj):
    if not DISABLE_TEE_STDOUT: pp(obj)
    log_pp(obj)

def tee_noted(s):
    tee(Style.DIM+Fore.GREEN+str(s)+Style.RESET_ALL)

def tee_noteb(s):
    tee(Style.BRIGHT+Fore.GREEN+str(s)+Style.RESET_ALL)

def tee_note(s):
    tee(Fore.GREEN+str(s)+Style.RESET_ALL)

def tee_warnb(s):
    tee(Style.BRIGHT+Fore.RED+'## warning: {}'.format(s)+Style.RESET_ALL)

def tee_warn(s):
    tee(Fore.RED+'## warning: {}'.format(s)+Style.RESET_ALL)

def tee_errorb(s):
    tee(Style.BRIGHT+Fore.RED+'## ERROR: {}'.format(s)+Style.RESET_ALL)

def tee_error(s):
    tee(Fore.RED+'## ERROR: {}'.format(s)+Style.RESET_ALL)

def tee_fatal(s):
    tee(Back.RED+Fore.BLACK+'################################################################################'.format(s)+Style.RESET_ALL)
    tee(Back.RED+Fore.BLACK+'## FATAL ERROR: {}'.format(s)+Style.RESET_ALL)
    tee(Back.RED+Fore.BLACK+'################################################################################'.format(s)+Style.RESET_ALL)
    do_exit()

def tee_start_fatal():
    tee(Back.RED+Fore.BLACK, _end='')
def tee_start_errorb():
    tee(Style.BRIGHT+Fore.RED, _end='')
def tee_start_error():
    tee(Fore.RED, _end='')
def tee_reset():
    tee(Style.RESET_ALL, _end='')

######################################################
#### more helper functions
######################################################

def print_time(sec):
    if sec == 0:
        return '00s'
    retval = ''
    if sec > 86400:
        retval = retval + '{}d'.format(str(int(sec//86400)).zfill(2))
        sec = sec % 86400
    if sec > 3600:
        retval = retval + '{}h'.format(str(int(sec//3600)).zfill(2))
        sec = sec % 3600
    if sec > 60:
        retval = retval + '{}m'.format(str(int(sec//60)).zfill(2))
        sec = sec % 60
    if sec > 0:
        retval = retval + '{}s'.format(str(int(sec)).zfill(2))
    return retval

def for_all_combinations(ix, to_enumerate, enumerated_values, func, func_arg):
    if ix == len(to_enumerate):
        return [func(enumerated_values, func_arg)]
    else:
        field = list(to_enumerate.keys())[ix]
        retlist = []
        for value in to_enumerate[field]:
            enumerated_values[field] = value
            retlist += for_all_combinations(1+ix, to_enumerate, enumerated_values, func, func_arg)
        return retlist

def quoted(col_name, value, quote_char="'"):
    retval = str(value)
    if col_name not in g['data_fields']: return retval
    if g['data_fields'][col_name]['coltype'].upper() != 'TEXT': return retval
    return quote_char + retval + quote_char

######################################################
#### parse command line args
######################################################

def do_argparse(cmdline_args_list=None, define_experiment_func=None):
    global g
    global MULTITHREAD_CREATEDB
    global MULTITHREAD_PLOT

    do_init_basic_functions()
    # print("g={}".format(g))

    parser = argparse.ArgumentParser(description='Run a complete experimental suite and produce plots.')
    if not define_experiment_func:
        parser.add_argument('user_exp_py', help='python3 file containing a user experiment definition (see microbench_experiments/tutorial and microbench_experiments/example/_user_experiment.py for an example)')

    parser.add_argument('-t', '--testing', action='store_true', help='enable testing mode (determines True/False value of variable <args.testing> that can be used to define experimental parameters differently to enable quick testing of a wide variety of cases, by, e.g., limiting the number of thread counts tested, reducing the time limit for experiments, setting the number of repeated trials to one, and disabling prefilling')
    parser.set_defaults(testing=False)
    parser.add_argument('-c', '--compile', action='store_true', help='enables compilation (disables reuse of old binaries)')
    parser.set_defaults(compile=False)
    parser.add_argument('-r', '--run', action='store_true', help='enables running experiments (disables reuse of old data files)')
    parser.set_defaults(run=False)
    parser.add_argument('-d', '--database', dest='createdb', action='store_true', help='enables recreation of sqlite database (disables reuse of old database)')
    parser.set_defaults(createdb=False)
    parser.add_argument('-p', '--plot', action='store_true', help='enables creation of plots')
    parser.set_defaults(plot=False)
    parser.add_argument('-w', '--website', dest='pages', action='store_true', help='enables creation of html website to show plots/data')
    parser.set_defaults(pages=False)
    parser.add_argument('-z', '--zip', action='store_true', help='enables creation of zip file containing results')
    parser.set_defaults(zip=False)
    parser.add_argument('-s', '--rerun-steps', nargs='+', type=int, help='reruns a list of {__step}s')
    parser.add_argument('-q', '--query', help='runs a read-only sqlite query (reusing the existing database) and prints the resulting dataframe')
    parser.set_defaults(query='')
    parser.add_argument('--deploy-to', nargs=1, metavar='DIR', help='run tools/deploy_dir.sh on the data directory to deploy the website to cs.uwaterloo.ca/~t35brown/rift/preview/<DIR>')
    parser.add_argument('--failed-list', action='store_true', help='list steps (data files) that failed validation')
    parser.set_defaults(failed_list=False)
    parser.add_argument('--failed-where', action='store_true', help='print sqlite WHERE clause for matching all data files that failed validation')
    parser.set_defaults(failed_where=False)
    parser.add_argument('--failed-details', action='store_true', help='print dataframe containing run_param details for all data files that failed validation')
    parser.set_defaults(failed_details=False)
    parser.add_argument('--failed-query-cols', nargs='+', metavar='COL', help='print dataframe containing the specified columns for all data files that failed validation')
    parser.add_argument('--debug-seq', action='store_true', help='force construction of sqlite db and plots to be single threaded (sequential)')
    parser.set_defaults(debug_seq=False)
    parser.add_argument('--debug-deploy', action='store_true', help='debug: run tools/deploy_dir.sh on the data directory')
    parser.set_defaults(debug_deploy=False)
    # parser.add_argument('--debug-no-plotcmd', dest='debug_exec_plotcmd', action='store_false', help='debug: do not exec the plot cmd')
    # parser.set_defaults(debug_exec_plotcmd=True)
    parser.add_argument('--continue-on-warn-agg-row-count', action='store_true', help='allow execution to continue despite any warnings about aggregating a possibly incorrect number of rows into some data point(s) in a plot')
    parser.set_defaults(continue_on_warn_agg_row_count=False)

    if cmdline_args_list:
        args = parser.parse_args(cmdline_args_list)
    else:
        args = parser.parse_args()

    # args = parser.parse_args()

    if len(sys.argv) < 2:
        parser.print_help()
        do_exit()

    if args.debug_seq:
        MULTITHREAD_CREATEDB = False
        MULTITHREAD_PLOT = False

    ## fun user's define_experiment function
    if not define_experiment_func:
        fname = args.user_exp_py
        sys.path.append(os.path.dirname(fname))
        module_filename = os.path.relpath(fname, os.path.dirname(fname)).replace('.py', '')
        import importlib
        mod_user_experiment = importlib.import_module(module_filename)
        mod_user_experiment.define_experiment(g, args)
    else:
        define_experiment_func(g, args)

    ## set sane defaults for parameters missing in user config
    if 'file_data' not in g:
        g['file_data'] = 'data{__step}.txt'
    if '__dir_data' not in g['replacements']:
        g['replacements']['__dir_data'] = os.getcwd() + '/data'
    if '__dir_run' not in g['replacements']:
        g['replacements']['__dir_run'] = os.getcwd()
    if '__dir_compile' not in g['replacements']:
        g['replacements']['__dir_compile'] = os.getcwd()
    if '__dir_plots' not in g['replacements']:
        g['replacements']['__dir_plots'] = g['replacements']['__dir_data'] + '/_plots'

    add_data_field(g, '__hostname')
    add_data_field(g, '__step')
    add_data_field(g, '__file_data')
    add_data_field(g, '__cmd_run')

    log('## Script running with the following parameters:')
    log()
    log_pp(g)

    if args.testing:
        log()
        tee_note('#########################################')
        tee_note('## WARNING: RUNNING IN TESTING MODE')
        tee_note('#########################################')
    # else:
    #     tee_note('## NOTE: RUNNING IN PRODUCTION MODE')

    # print()
    # print("do_argparse: g={}".format(g))

    return args

#########################################################################
#### Compile into bin_dir
#########################################################################

def do_compile(args):
    if args.compile:
        # tee()
        # tee_note('## Removing old binaries')

        # tee_replace_and_run('rm -rf {__dir_run} 2>&1')
        # tee_replace_and_run('mkdir {__dir_run}', exit_on_error=True)

        tee()
        tee_note(replace('## Compiling in {__dir_run}'))

        log()
        log(replace('running "cd {__dir_compile} ; ' + g['cmd_compile'] + '"'))
        log_replace_and_run('cd {__dir_compile} ; ' + g['cmd_compile'], exit_on_error=True)
    # else:
    #     tee()
    #     tee_note('## Skipping compilation because of cmdline arg...')

#########################################################################
#### Run trials
#########################################################################

def run_param_loops(ix, args, actually_run=True):
    global step
    global __rerunstep

    # log('run_param_loop ix={} len(runparams)={} len(replacements)={}'.format(ix, len(g['run_params']), len(g['replacements'])))
    # log_pp(g['run_params'])
    # log_pp(g['replacements'])

    if ix == len(g['run_params']):
        ## check if we should filter out this combination of parameters (and avoid running it)
        ## according to the run_params_filters

        ## note: we must do this after ALL 'replacements' are set,
        ##       or we may filter incorrectly!
        for key in g['run_params'].keys():
            filter_func = g['run_params_filters'][key]
            if filter_func:
                if not filter_func(g['replacements']): return

        ## this configuration is NOT filtered out, so continue!

        step += 1

        ## if we are trying to rerun only certain steps, skip any we're not trying to rerun.
        if args.rerun_steps and step not in args.rerun_steps: return
        __rerunstep += 1 ## this is incremented only if we pass the rerun steps filtering stage, so it is the SAME as step in runs where we're rerunning a few steps, and it's a filtered number otherwise

        g['replacements']['__step'] = str(step).zfill(6)
        g['replacements']['__cmd_run'] = replace(g['cmd_run'])
        g['replacements']['__file_data'] = replace(g['file_data']).replace(' ', '_')
        g['replacements']['__path_data'] = g['replacements']['__dir_data'] + '/' + g['replacements']['__file_data']

        # print('{}'.format(file_data_replaced))
        # print('{}'.format(cmd_run_replaced))
        # log()
        # log_pp(g['replacements'])

        cmd = g['replacements']['__cmd_run']
        fname = g['replacements']['__file_data']
        path = g['replacements']['__path_data']
        host = g['replacements']['__hostname']

        if actually_run:
            tee()
            tee('step {} / {}: {} -> {}'.format(str(__rerunstep).zfill(6), str(maxstep).zfill(6), cmd, Fore.BLUE+ os.path.relpath(path) + Style.RESET_ALL))

        ## remember the data file we are about to (or would) create
        g['data_file_paths'].append(path)

        if actually_run:
            before_cmd_sec = int(shell_to_str('date +%s'))
            # shell_to_str('echo "__step={}\n__cmd_run={}\n__file_data={}\n__path_data={}\n__hostname={}\n" > {}'.format(str(step).zfill(6), cmd, os.path.relpath(fname), os.path.relpath(path), host, path), exit_on_error=True)

            ## print some extra fields to the start of the file
            with open(path, 'a') as f:
                f.write('__step={}\n'.format(str(step).zfill(6)))
                f.write(replace('__cmd_run={__cmd_run}\n'))
                f.write('__file_data={}\n'.format(os.path.relpath(fname)))
                f.write('__path_data={}\n'.format(os.path.relpath(path)))
                f.write('__hostname={}\n'.format(host))

            ## actually run experimental command
            log(replace('running "cd {__dir_run} ; {__cmd_run} >> {__path_data} 2>&1"'))
            retval = replace_and_run('cd {__dir_run} ; {__cmd_run} >> {__path_data} 2>&1', exit_on_error=False)

            ## error handling
            if isinstance(retval, int):
                if retval == 124:
                    ## timed out
                    if 'timeout' in g['replacements']['__cmd_run']: replace_and_run('cd {__dir_run} ; echo "timeout=true" >> {__path_data}')
                    tee_warnb("timeout command killed the run command")
                else:
                    ## some legit crash
                    if 'timeout' in g['replacements']['__cmd_run']: replace_and_run('cd {__dir_run} ; echo "timeout=false" >> {__path_data}')
                    tee_warnb("the run command crashed...")
                    # raise #sys.exit(-1)
            else:
                if 'timeout' in g['replacements']['__cmd_run']: replace_and_run('cd {__dir_run} ; echo "timeout=false" >> {__path_data}')

            ## append to the data file any run_params that are MISSING from it
            with open(path, 'a') as f:
                for param in g['run_params'].keys():
                    if not is_in_grep_results(g, path, param):
                        # print('DEBUG: appending field {}={} to {}'.format(param, g['replacements'][param], path))
                        f.write('{}={}\n'.format(param, g['replacements'][param]))

            ## update progress info
            curr_sec = int(shell_to_str('date +%s'))
            elapsed_sec = curr_sec - started_sec
            frac_done = __rerunstep / maxstep
            elapsed_sec_per_done = elapsed_sec / __rerunstep
            remaining_sec = elapsed_sec_per_done * (maxstep - __rerunstep)
            total_est_sec = elapsed_sec + remaining_sec
            tee_note('(' + print_time(curr_sec - before_cmd_sec) + ' run, ' + print_time(elapsed_sec) + ' elapsed, ' + print_time(remaining_sec) + ' est. remaining, est. total ' + print_time(total_est_sec) + ')')

    else:
        param_list = list(g['run_params'].keys())
        param = param_list[ix]
        for value in g['run_params'][param]:
            g['replacements'][param] = value
            run_param_loops(1+ix, args, actually_run)

def compute_max_step(ix):
    global maxstep
    if ix == 0: maxstep = 0
    if ix == len(g['run_params']):
        ## check if we should filter out this combination of parameters (and avoid COUNTING it)
        ## note: we must do this after ALL 'replacements' are set, or we may filter incorrectly!
        for key in g['run_params'].keys():
            filter_func = g['run_params_filters'][key]
            if filter_func:
                if not filter_func(g['replacements']): return
        maxstep += 1
    else:
        param_list = list(g['run_params'].keys())
        param = param_list[ix]
        for value in g['run_params'][param]:
            g['replacements'][param] = value
            compute_max_step(1+ix)

def do_run(args):
    global step
    global __rerunstep
    global maxstep
    global started_sec
    global started

    # maxstep = 1
    # for key in g['run_params']:
    #     maxstep = maxstep * len(g['run_params'][key])

    if args.rerun_steps:
        maxstep = len(args.rerun_steps)
    else:
        compute_max_step(0)

    started_sec = int(shell_to_str('date +%s'))
    # tee("started_sec={}".format(started_sec))

    started = datetime.datetime.now()

    if args.run or args.rerun_steps:

        if not args.rerun_steps:
            tee()
            tee_note('## Removing old data')

            tee_replace_and_run('rm -rf {__dir_data} 2>&1')
            tee_replace_and_run('mkdir {__dir_data}', exit_on_error=True)

        tee()
        tee_note('## Running trials (starting at {})'.format(started))

        run_param_loops(0, args)
    else:
        # tee()
        # tee_note('## Skip running (reuse old data)')
        step = 0
        __rerunstep = 0
        run_param_loops(0, args, actually_run=False)

#########################################################################
#### Create sqlite database
#########################################################################

def do_createdb(args):
    global con
    global cur
    global db_path
    global validation_errors

    db_path = '{}/output_database.sqlite'.format(g['replacements']['__dir_data'])

    if args.createdb:
        tee()
        tee_note('## Creating sqlite database')
        shell_to_str('rm -f {}'.format(db_path), exit_on_error=True)
    # else:
    #     tee_note('## Skipping creation of sqlite database (using existing db)')

    tee_replace_and_run('if [ ! -e {__dir_data} ] ; then mkdir {__dir_data} 2>&1 ; fi', exit_on_error=False)
    con = sqlite3.connect(db_path)
    cur = con.cursor()
    g['con'] = con
    g['cur'] = cur

    if args.createdb:
        txn = 'CREATE TABLE data (__id INTEGER PRIMARY KEY'
        for key in g['data_fields'].keys():
            txn += ', ' + key + ' ' + g['data_fields'][key]['coltype']
        txn += ')'
        # print("\ntxn='{}'".format(txn))
        cur.execute(txn)
        con.commit()

        txn = 'CREATE TABLE failures (__id INTEGER PRIMARY KEY'
        for key in g['data_fields'].keys():
            txn += ', ' + key + ' ' + g['data_fields'][key]['coltype']
        txn += ')'
        # print("\ntxn='{}'".format(txn))
        cur.execute(txn)
        con.commit()

    #########################################################################
    #### Extract data from output files and insert into sqlite database
    #########################################################################

    if args.createdb:
        if 'data_fields' not in g or len(g['data_fields']) == 0:
            tee_warnb('define_experiment() has not called add_data_field, so run_experiment has no knowledge of how to construct a sqlite database. Skipping...')
        else:
            tee_note('Creating sqlite database from experimental data')
            tee()

            manager = multiprocessing.Manager()
            txns = manager.list()
            errors = manager.list()
            pattern = re.compile('[{][^}]*[}]')
            data_file_pattern = pattern.sub('*', g['file_data'])
            # print('data_file_pattern={}'.format(data_file_pattern))
            glob_results = glob.glob('{}/{}'.format(get_dir_data(g), data_file_pattern))
            # print('len(glob_results)={}'.format(len(glob_results)))
            # print('glob_results[0:10]={}'.format(glob_results[0:10]))
            # print('len(g["data_file_paths"])={}'.format(len(g['data_file_paths'])))
            # print('g["data_file_paths"][0:10]={}'.format(g['data_file_paths'][0:10]))
            # exit(-1)
            # if len(g['data_file_paths']) != len(glob_results):
            #     tee_warnb('data file count that would be produced by running ({}) does NOT match available data count ({})'.format(len(g['data_file_paths']), len(glob_results)))

            if MULTITHREAD_CREATEDB:
                pool = multiprocessing.Pool()
                pool.map(process_single_data_file, [(f, txns, errors) for f in glob_results])
                pool.close()
                pool.join()
            else:
                for data_file_path in glob_results:
                    process_single_data_file((data_file_path, txns, errors))

            validation_errors += errors

            sqlscript=';'.join(txns)
            # tee_noted(sqlscript)
            tee_noted('running sqlite script of length {}'.format(len(sqlscript)))
            cur.executescript(sqlscript)
            con.commit()

            if len(g['data_file_paths']) != len(glob_results):
                tee_warnb('data file count that would be produced by running ({}) does NOT match available data count ({})'.format(len(g['data_file_paths']), len(glob_results)))

def process_single_data_file(args):
    data_file_path, output, error_output = args
    fields = ''
    for field in g['data_fields'].keys():
        fields += ('' if fields == '' else ', ') + field

    short_fname = os.path.relpath(data_file_path)
    log()
    tee('processing {}'.format(short_fname))
    g['log'].flush()

    good = True
    values = ''
    for field in g['data_fields'].keys():
        extractor = g['data_fields'][field]['extractor']
        validator = g['data_fields'][field]['validator']

        value = extractor(g, data_file_path, field)
        if not validator(g, value):
            # tee('validation error: validator={} value={}'.format(validator, value))
            error_output.append(short_fname + ': field "' + str(field) + '" with value "' + str(value) + '"')
            good = False
            # return ## SKIP EVERYTHING AFTER FIRST VALIDATION ERROR
        else:
            log('    extractor for field={} gets value={} valid={}'.format(field, value, validator(g, value)))

        if "'" in str(value):
            tee("\n")
            tee_fatal("value {} contains single quotes! this will break sql insertion statements! cannot proceed. exiting...".format(value))

        if g['data_fields'][field]['coltype'] != 'TEXT' and str(value) == '':
            value = 0
        values += '{}{}'.format(('' if values == '' else ', '), quoted(field, value))

    if good:
        txn = 'INSERT INTO data ({}) VALUES ({})'
    else:
        txn = 'INSERT INTO failures ({}) VALUES ({})'

    txn = txn.format(fields, values)
    log()
    log(txn)
    g['log'].flush()
    # cur.execute(txn)
    output.append(txn)

#########################################################################
#### Produce plots
#########################################################################

def cf_any(col_name, col_values):
    return True

def cf_not_intrinsic(col_name, col_values):
    if len(col_name) >= 2 and col_name[0:2] == '__':
        # print('cf_not_intrinsic: remove {}'.format(col_name))
        return False
    return True

def cf_not_identical(col_name, col_values):
    first = col_values[0]
    for val in col_values:
        if val != first: return True
    # print('cf_not_identical: remove {}'.format(col_name))
    return False

def cf_run_param(col_name, col_values):
    if col_name in g['run_params'].keys(): return True
    # print('cf_run_param: remove {}'.format(col_name))

def cf_not_in(exclusion_list):
    def cf_internal(col_name, col_values):
        return col_name not in exclusion_list
        # print('cf_not_in: remove {}'.format(col_name))
    return cf_internal

## assumes identical args
def cf_and(f, g):
    def composed_fn(col_name, col_values):
        return f(col_name, col_values) and g(col_name, col_values)
    return composed_fn
def cf_andl(list):
    def composed_fn(col_name, col_values):
        for f in list:
            if not f(col_name, col_values): return False
        return True
    return composed_fn

# def get_column_filter_bitmap(headers, rows, column_filter=cf_any):
#     ## compute a bitmap that dictates which columns should be included (not filtered out)
#     include_ix = [-1 for x in range(len(headers))]
#     for i, col_name in zip(range(len(headers)), headers):
#         col_values = [row[i] for row in rows]
#         include_ix[i] = column_filter(col_name, col_values)
#     return include_ix

## quote_text only works if there are headers
## column_filters only work if there are headers (even if they filter solely on the data)
## sep has no effect if columns are aligned
def table_to_str(table, headers=None, aligned=False, quote_text=True, sep=' ', column_filter=cf_any, row_header_html_link='', do_pages=False):
    assert(headers or column_filter == cf_any)
    assert(headers or not quote_text)
    assert(sep == ' ' or not aligned)

    # tee("table={}".format(table))
    iterable = [row for index, row in table.iterrows()] if isinstance(table, pd.DataFrame) else table
    # tee("iterable={}".format(iterable))
    # g['log'].flush()


    buf = io.StringIO()

    ## first check if headers is
    ## 1. a list of string, or                          (manual input list)
    ## 2. a list of tuples each containing one string   (output of sqlite query for field names)
    ## and if needed transform #2 into #1.
    if headers:
        if sum([len(row) for row in headers]) == len(headers):
            ## we are in case #2
            headers = [row[0] for row in headers]

        ## also compute a bitmap that dictates which columns will be included (not filtered out)
        include_ix = [-1 for x in range(len(headers))]
        for i, col_name in zip(range(len(headers)), headers):
            col_values = [row[i] for row in iterable]
            include_ix[i] = column_filter(col_name, col_values)
        # log('include_ix={}'.format(include_ix))

    if not aligned:
        if headers:
            first = True

            if headers and row_header_html_link != '' and row_header_html_link in headers:
                buf.write(row_header_html_link)
                first = False

            for i, col_name in zip(range(len(headers)), headers):
                if include_ix[i]:
                    if not first: buf.write(sep)
                    buf.write(col_name)
                    first = False
            buf.write('\n')

        for row in iterable:
            first = True

            if headers and row_header_html_link != '' and row_header_html_link in headers:
                buf.write('</pre><a href="{}"><pre>{}</pre></a><pre>'.format(row_header_html_link, row_header_html_link))
                first = False

            for i, col in zip(range(len(row)), row):
                if not headers or include_ix[i]:
                    if not first: buf.write(sep)
                    s = str(col)
                    if headers: s = quoted(headers[i], s)
                    buf.write( s )
                    first = False
            buf.write('\n')

    ## aligned
    else:
        if headers:
            cols_w = [ max( [len(str(row[i])) for row in iterable] + [len(headers[i])] ) for i in range(len(headers)) ]
        else:
            cols_w = [ max( [len(str(row[i])) for row in iterable] ) for i in range(len(iterable[0])) ]

        link_text = '[view raw data]'

        if headers:
            ## assumption: if there is a row_header_html_link, it is the first column (i.e., a true row header)...
            if headers and row_header_html_link != '' and row_header_html_link in headers:
                buf.write( row_header_html_link.ljust(3+max(len(row_header_html_link), len(link_text))) )

            for i, col_name in zip(range(len(headers)), headers):
                if include_ix[i]:
                    buf.write( str(col_name).ljust(3+cols_w[i]) )
            buf.write('\n')

        for row in iterable:
            if headers and row_header_html_link != '' and row_header_html_link in headers:
                col_of_row_header = list(filter(lambda __i: (headers[__i] == row_header_html_link), range(len(headers))))[0]
                value_of_row_header = row[col_of_row_header]
                desired_width = 3+max(len(link_text), len(row_header_html_link)) - 1
                spaces_to_add = ''
                if desired_width > len(str(value_of_row_header)):
                    spaces_to_add = '.' * (desired_width - len(str(value_of_row_header)))

                ## forgive me for what i'm about to do...
                ## horrible software engineering, but here,
                ## right in the middle of rendering this table,
                ## i'm going to create an html counterpart to the file "value_of_row_header"
                ## so i can link that instead...
                if do_pages:
                    with open(get_dir_data(g) + '/' + value_of_row_header, 'r') as f:
                        file_contents = f.read()
                    new_file_name = value_of_row_header.replace('.txt', '.html')
                    with open(get_dir_data(g) + '/' + new_file_name, 'w') as f:
                        f.write(template_data_html.replace('{template_content}', file_contents))

                    buf.write('</pre><a href="{}"><pre>{}</pre></a><pre>{}</pre><pre>'.format( new_file_name, link_text, spaces_to_add ))

            for i, col in zip(range(len(row)), row):
                if not headers or include_ix[i]:
                    s = str(col)
                    if headers: s = quoted(headers[i], s)
                    buf.write( s.ljust(3+cols_w[i]) )
            buf.write('\n')

    return buf.getvalue()

def get_amended_where(varying_cols_vals, where_clause):
    ## create new where clause (including these varying_cols_vals in the filter)
    log('preliminary where_clause={}'.format(where_clause))
    where_clause_new = where_clause
    if where_clause_new.strip() == '':
        where_clause_new = 'WHERE (1==1)'
    for key in varying_cols_vals.keys():
        where_clause_new += ' AND {} == {}'.format(key, quoted(key, varying_cols_vals[key]))
    log("amended where_clause_new={}".format(where_clause_new))
    return where_clause_new

## crucial function: produce actual dataframe to be plotted
def get_records_plot(series, x, y, where_clause_new):
    assert(y)

    if isinstance(y, str):
        ## handle single y-value column
        header_records = []
        if series: header_records += [series]
        if x: header_records += [x]
        header_records += [y]

        ## perform sqlite query to fetch raw data
        txn = 'select {} from data {} order by __step asc'.format(','.join(header_records), where_clause_new)
        log('txn={}'.format(txn))
        data_records = pd.read_sql_query(txn, get_connection())

    elif isinstance(y, list):
        ## handle list of multiple y-fields (for a stacked bar plot --- TODO: compatible with any other plot types?)
        assert(not series)
        assert(isinstance(x, str))

        ## perform sqlite query to fetch raw data
        fields_to_query = [x] + y
        txn = 'select {} from data {}'.format(','.join(fields_to_query), where_clause_new)
        log('txn={}'.format(txn))
        data_records = pd.read_sql_query(txn, get_connection())

        ## do pandas melt to convert columns into "tidy" data
        data_records = pd.melt(data_records, id_vars=[x], value_vars=y)

        header_records = []
        header_records += ['variable']  ## special "series" column created by pd.melt()
        if x: header_records += [x]
        header_records += ['value']     ## special "y-axis" column created by pd.melt()

        ## reorder columns to match header_records
        data_records = data_records[header_records]

        # tee_pp(header_records) ; tee_pp(data_records) ; g['log'].flush() ; sys.exit(1)

    else:
        tee_fatal('y should be a column name or list of column names... invalid value "{}"'.format(y))

    return header_records, data_records

def get_records_plot_from_set(plot_set, where_clause_new):
    return get_records_plot(plot_set['series'], plot_set['x_axis'], plot_set['y_axis'], where_clause_new)

def get_connection(exp_dict=None):
    if not exp_dict: exp_dict = g
    return exp_dict['con']

def get_cursor(exp_dict=None):
    if not exp_dict: exp_dict = g
    return exp_dict['cur']

def select_to_dataframe(txn):
    return pd.read_sql_query(txn, get_connection())

def get_headers():
    return list(select_to_dataframe('select * from data where 1==0').columns.values)

def get_records_plot_full(where_clause_new):
    header_records = get_headers()

    ## then fetch the data
    txn = 'select * from data {} order by __step asc'.format(where_clause_new)
    log('txn={}'.format(txn))
    cur.execute(txn)
    data_records = cur.fetchall()
    return header_records, data_records

def get_plot_tool_path(plot_set):
    series = plot_set['series']     ## check if we have a series column for this set of plots (and if so, construct an appropriate string for our query expression)

    ## determine which command line tool to use to produce the plot
    ## and do a quick check to see if it's legal to use it with the given options
    ptype = plot_set['plot_type'].lower()
    if ptype == 'bars':
        plot_tool_path = 'plotbars.py' if series else 'plotbar.py'
    elif ptype == 'line':
        plot_tool_path = 'plotlines.py' if series else 'plotline.py'
    elif ptype == 'hist2d':
        plot_tool_path = 'plothist2d.py'
        if series: tee_fatal('cannot plot {} with series'.format(ptype))
    elif ptype == 'histogram':
        plot_tool_path = 'histogram.py'
        if series: tee_fatal('cannot plot {} with series')
        if plot_set['y_axis']: tee_fatal('cannot plot {} with y_axis')

    plot_tool_path = get_dir_tools(g) + '/' + plot_tool_path
    return plot_tool_path

def sanity_check_plot(args, plot_set, where_clause_new, header_records, plot_txt_filename=''):
    if not isinstance(plot_set['y_axis'], str):
        return   ## if y-axis is a list, or something else, this sanity check procedure won't work...

    series = plot_set['series']     ## check if we have a series column for this set of plots (and if so, construct an appropriate string for our query expression)
    if series != '' and series != 'None' and series != None: series += ', '

    ## perform sqlite query to fetch aggregated data
    log('sanity check: counting number of rows being aggregated into each plot data point')
    txn = 'SELECT count({}), {}{} FROM data {} GROUP BY {}{}'.format(plot_set['y_axis'], series, plot_set['x_axis'], where_clause_new, series, plot_set['x_axis'])
    cur.execute(txn)
    data_records = cur.fetchall()
    row_counts = [row[0] for row in data_records]

    ## don't bother with this sanity check if
    #  (a) we don't know how many trials there are, or
    #  (b) this is a legend only graph (data doesn't matter)
    if '__trials' in g['run_params'].keys() and 'legend-only' not in plot_set['plot_cmd_args'] and not plot_set['name'].endswith('legend.png'):
        num_trials = len(g['run_params']['__trials'])
        if any([x != num_trials for x in row_counts]):
            #log('txn={}'.format(txn))
            # log_pp(data_records)

            s = '\n'
            s += ('################################################################################') + '\n'
            s += ('## WARNING: some data point(s) in {} '.format(os.path.relpath(plot_txt_filename))) + '\n'
            s += ('##          aggregate an unexpected number of rows! ') + '\n'
            s += ('################################################################################') + '\n'
            s += '\n'
            s += ('  most likely we should be aggregating ONE row per trial, and ther are {} trials.\n'.format(num_trials))
            s += ('  however, row counts for data point(s) are:\n')
            s += ('      {}\n'.format(row_counts))
            s += ('  if row counts are smaller than expected, you may have filtered incorrectly.\n')
            s += ('      where clause: {}\n'.format(where_clause_new))
            s += ('  if row counts are larger than expected, you may have forgotten a run_param\n')
            s += ('  that should be part of varying_cols_list for the plot.\n')
            s += ('\n')

            ## fetch an example set of possibly problematic rows
            s += '## Fetching an example set of possibly problematic rows:\n'

            ## build where clause that incorporates filtering to some problematic row in data_records

            for row in data_records:
                if row[0] != num_trials: problem_col_values = row[1:]

            problem_col_names = []
            if series: problem_col_names.append(plot_set['series'])
            problem_col_names.append(plot_set['x_axis'])

            print("sanity_check_plot: problem_col_names={} problem_col_values={} data_records={}".format(problem_col_names, problem_col_values, data_records))

            assert(len(problem_col_names) == len(problem_col_values))

            where_clause_problem = where_clause_new
            assert(where_clause_problem != None and where_clause_problem != 'None' and where_clause_problem.strip() != '')

            for col_name, col_value in zip(problem_col_names, problem_col_values):
                where_clause_problem += ' AND ' + col_name + ' == ' + quoted(col_name, col_value)

            txn = 'SELECT * FROM data {}'.format(where_clause_problem)
            tee('txn={}'.format(txn))
            cur.execute(txn)
            data_records = cur.fetchall()

            s += table_to_str(data_records, header_records, aligned=True, do_pages=args.pages)

            ## NOTE TO SELF: show the power of this mechanism by including two "distribution" values but neglecting to add that field to a plot!!

            s += '\n'
            s += '## FILTERING TO REMOVE COLUMNS WITH NO DIFFERENCES, AND INTRINSIC COLUMNS...\n'
            s += '##     any problem is likely in one of these columns:\n'
            s += table_to_str(data_records, header_records, aligned=True, column_filter=cf_and(cf_not_intrinsic, cf_not_identical), do_pages=args.pages)

            tee_start_errorb()
            tee(s)
            tee_reset()
            g['sanity_check_failures'].append(s)

            if not args.continue_on_warn_agg_row_count:
                tee('EXITING... (run with --continue-on-warn-agg-row-count to disable this.)')
                tee()
                do_exit()

def process_single_plot(args, plot_set, varying_cols_vals, where_clause):
    #############################################################
    ## Data filter and fetch
    #############################################################

    where_clause_new = get_amended_where(varying_cols_vals, where_clause)
    header_records, data_records = get_records_plot_from_set(plot_set, where_clause_new)

    tee('process_single_plot: varying_cols_vals={} where_clause={} where_clause_new={}'.format(varying_cols_vals, where_clause, where_clause_new))

    #############################################################
    ## Dump PLOT DATA to a text file
    #############################################################

    ## now we have the data we want to plot...
    ## write it to a text file (so we can feed it into a plot command)
    log_pp(data_records)

    plot_filename = g['replacements']['__dir_data'] + '/' + replace(plot_set['name']).replace(' ', '_')
    plot_txt_filename = plot_filename.replace('.png', '.txt')

    g['replacements']['__plot_filename'] = plot_filename
    g['replacements']['__plot_txt_filename'] = plot_txt_filename

    tee('plot {}'.format(os.path.relpath(plot_filename)))
    tee('plot {}'.format(os.path.relpath(plot_txt_filename)))
    log('plot_filename={}'.format(plot_filename))
    log('plot_txt_filename={}'.format(plot_txt_filename))

    plot_txt_file = open(plot_txt_filename, 'w')
    plot_txt_file.write(table_to_str(data_records, quote_text=False, aligned=False, do_pages=args.pages))
    plot_txt_file.close()

    #############################################################
    ## Create a plot
    #############################################################

    retval = ''
    plot_type = plot_set['plot_type']
    if isinstance(plot_type, types.FunctionType):
        title = ''
        if plot_set['title'] != '':
            if '"' in plot_set['title']:
                tee_fatal('plot title cannot have a double quote character ("); this occurred in title {}'.format(plot_set['title']))
            title = replace(plot_set['title'])

        if MULTITHREAD_PLOT:
            plot_args = dict(
                  data=data_records
                , column_filters=varying_cols_vals
                , filename=plot_filename
                , series_name=plot_set['series']
                , x_name=plot_set['x_axis']
                , y_name=plot_set['y_axis']
            )
            if isinstance(plot_set['plot_cmd_args'], dict):
                plot_args['config'] = plot_set['plot_cmd_args']
            retval = plot_type, plot_args
        else:
            # print('g={}'.format(g))
            plot_type(
                  data=data_records
                , column_filters=varying_cols_vals
                , filename=plot_filename
                , series_name=plot_set['series']
                , x_name=plot_set['x_axis']
                , y_name=plot_set['y_axis']
                # , exp_dict=g
            )
            if isinstance(plot_set['plot_cmd_args'], dict):     ## new
                plot_args['config'] = plot_set['plot_cmd_args'] ## new
            plt.close()

    else:
        assert(isinstance(plot_type, str))

        title = ''
        if plot_set['title'] != '':
            if '"' in plot_set['title']:
                tee_fatal('plot title cannot have a double quote character ("); this occurred in title {}'.format(plot_set['title']))
            title = '-t "{}"'.format(replace(plot_set['title']))

        g['replacements']['__title'] = title
        g['replacements']['__plot_cmd_args'] = plot_set['plot_cmd_args']

        plot_style_hooks_file = ''
        if plot_set['plot_style_hooks_file'] != '':
            plot_style_hooks_file = ' --style-hooks {}'.format(plot_set['plot_style_hooks_file'])

        ## handle multiple y-axis fields (makeshift series field "variable" created by get_records_plot)
        if not plot_set['series'] and isinstance(plot_set['y_axis'], list):
            plot_set['series'] = 'variable'
            g['replacements']['__plot_tool_path'] = get_plot_tool_path(plot_set)
            plot_set['series'] = ''
        else:
            g['replacements']['__plot_tool_path'] = get_plot_tool_path(plot_set)

        cmd='{__plot_tool_path} -i {__plot_txt_filename} -o {__plot_filename} {__title} {__plot_cmd_args}' + plot_style_hooks_file
        log('running plot_cmd {}'.format(replace(cmd)))
        g['log'].flush()
        # if args.debug_exec_plotcmd: ## debug option to disable plot cmd exec
        if not MULTITHREAD_PLOT: log_replace_and_run(cmd, exit_on_error=True)
        retval = replace(cmd)

    #############################################################
    ## Dump FULL COLUMN DATA to the text file
    #############################################################
    ##
    ## fetch data with full detail (all columns)
    ## and append it to the existing text result
    ## (AFTER we've used that text to plot)
    ##

    header_records_full, data_records_full = get_records_plot_full(where_clause_new)

    ## dump to disk
    plot_txt_full_filename= plot_filename.replace('.png', '_full.txt')
    plot_txt_full_filename = open(plot_txt_full_filename, 'w')
    plot_txt_full_filename.write(table_to_str(data_records_full, header_records_full, aligned=True, row_header_html_link='__file_data', do_pages=args.pages))
    plot_txt_full_filename.close()

    #############################################################
    ## Sanity checks on PLOT
    #############################################################

    sanity_check_plot(args, plot_set, where_clause_new, header_records_full, plot_txt_filename)

    return retval

def get_where(plot_set):
    where_clause = ''
    if plot_set['filter'] != '' and plot_set['filter'] != 'None' and plot_set['filter'] != None:
        where_clause = 'WHERE ({})'.format(plot_set['filter'])
    return where_clause

def get_where_from_dict(values):
    return get_amended_where(values, '')

def do_plot(args):
    if args.plot:
        plot_commands = []

        tee_note('## Removing old plots')
        tee_replace_and_run('rm -rf {__dir_plots} 2>&1')
        tee_replace_and_run('mkdir {__dir_plots}', exit_on_error=True)        

        tee()
        tee_note('## Creating plots')
        plot_set_num = 0
        for plot_set in g['plots']:
            plot_set_num += 1

            ## build a SET of plots (defined by the varying_cols_list of plot_set)

            tee()
            tee("# plot set {}".format(plot_set_num))
            tee_pp(plot_set)

            where_clause = get_where(plot_set)

            ## get distinct assignments of columns in varying_cols_list
            varying_cols = ','.join(plot_set['varying_cols_list'])
            if varying_cols:
                txn = 'SELECT DISTINCT {} FROM data {}'.format(varying_cols, where_clause)
                # log_pp(txn)
                cur.execute(txn)
                varying_cols_records = cur.fetchall()
            else:
                varying_cols_records = ['']

            #################################################################
            ## Sanity checks on PLOT SET
            #################################################################

            ## sanity check: varying cols list contains a field that is NOT a run_param
            if any([x not in g['run_params'].keys() for x in plot_set['varying_cols_list']]):
                # print(plot_set['varying_cols_list'])
                # print(g['run_params'].keys())
                # print([x not in g['run_params'].keys() for x in plot_set['varying_cols_list']])
                # print(list(filter(lambda col: col not in g['run_params'].keys(), plot_set['varying_cols_list'])))
                s = 'WARNING: plot_set {} list of varying columns {} contains field(s) {} that are NOT run_params! this is most likely a mistake, as the replacement token for such a field will NOT be well defined (since its value would be unique to a single TRIAL, rather than a plot)!'.format( plot_set['name'], plot_set['varying_cols_list'], list(filter(lambda col: col not in g['run_params'].keys(), plot_set['varying_cols_list'])) )
                tee(s)
                g['sanity_check_failures'].append(s)
                ## note: this is only a fatal error if the user tries to use such a field as a replacement token...

            ## sanity check: plot_set producing UNEXPECTED NUMBER of plots
            if plot_set['filter'] == '' or all([x not in plot_set['filter'] for x in plot_set['varying_cols_list']]):
                ## if there's no filter, or none of the varying cols are in the filter, we can predict # of plots
                # cur.execute('SELECT __path_data FROM data {}'.format(where_clause))
                # plot_run_files = [x[0] for x in cur.fetchall()]
                # log('g[\'run_params\']={} plot_set['varying_cols_list']={} lengths=\'{}\''.format(g['run_params'], plot_set['varying_cols_list'], [ len(g['run_params'][x]) for x in filter(lambda param: param in plot_set['varying_cols_list'], g['run_params'].keys()) ]))
                expected_plot_count = int(numpy.prod( [ len(g['run_params'][x]) for x in filter(lambda param: param in plot_set['varying_cols_list'], g['run_params'].keys()) ] ))

                if len(varying_cols_records) != expected_plot_count:
                    # tee('txn={}'.format(txn))
                    s = 'WARNING: expected plot_set {} to produce {} plots but it produces {}'.format(plot_set['name'], expected_plot_count, len(varying_cols_records))
                    tee(s)
                    g['sanity_check_failures'].append(s)

            ## sanity check: plot_set producing ZERO plots
            if len(varying_cols_records) == 0:
                tee('txn={}'.format(txn))
                ## ZERO is definitely a mistake so fail fast
                tee_fatal('plot_set {} produces ZERO plots'.format(plot_set['name']))

            #################################################################
            ## For each plot
            #################################################################

            ## for each distinct assignment
            for varying_cols_rowstr in varying_cols_records:
                log()
                log()
                # tee("varying_cols_rowstr={}".format(varying_cols_rowstr))

                ## build a dict for this assignment
                varying_cols_vals = dict()
                for i in range(len(varying_cols_rowstr)):
                    varying_cols_vals[plot_set['varying_cols_list'][i]] = varying_cols_rowstr[i]
                    g['replacements'][plot_set['varying_cols_list'][i]] = varying_cols_rowstr[i]
                #log("varying_cols_vals={}".format(varying_cols_vals))

                plot_cmd = process_single_plot(args, plot_set, varying_cols_vals, where_clause)
                if plot_cmd: plot_commands.append(plot_cmd)

        if MULTITHREAD_PLOT:
            curr_sec = int(shell_to_str('date +%s'))
            tee()
            tee_note('## Multithreaded plot command execution START at {}'.format(curr_sec))
            tee_noted('parallel execution of {} plotting commands'.format(len(plot_commands)))
            # tee_noted(str(plot_commands).replace(',', ',\n'))
            if len(plot_commands):
                pool = multiprocessing.Pool()
                pool.map(multithreaded_plotting_run, plot_commands)
                pool.close()
                pool.join()
            new_sec = int(shell_to_str('date +%s'))
            tee_note('## Multithreaded plot command execution FINISH duration {}'.format(new_sec - curr_sec))

    # else:
    #     tee()
    #     tee('## Skipping plot creation')

def multithreaded_plotting_run(plot_cmd):
    if isinstance(plot_cmd, tuple):
        plot_func, plot_args = plot_cmd
        plot_func(**plot_args)
    elif isinstance(plot_cmd, str):
        log(shell_to_str(plot_cmd, exit_on_error=True))
    else:
        tee_warnb('unsure how to handle plot_cmd argument that does not appear to be a function or a string: multithreaded_plotting_run(plot_cmd={})'.format(plot_cmd))

#########################################################################
#### Create html pages to show plots
#########################################################################

## todo:
# - preload? gif support?
#   - hover_html="onmouseover=\\\"this.src='${prefix_escaped}.gif'\\\" onmouseout=\\\"this.src='${prefix_escaped}.png'\\\""
#   - preloadtags="${preloadtags}<link rel=\\\"preload\\\" as=\\\"image\\\" href=\\\"${prefix_escaped}.gif\\\">"
#
## design sketch:
#
# - design build_html_page_set and descendents so they don't produce pages/tables/rows/cols
#   for data that is FILTERED OUT.
#   - i think i'll do this by writing a function that looks at the produced image files,
#     filters them by the plot_set's page_field_list, table_field, row_field, column_field,
#     and returns the filtered list.
#   - the filtering will likely be much easier if i DELETE old entries from the replacement
#     dict that i'm passing around these functions.
#     (so it doesn't filter too much by accident)
#   - with this function, i can easily check at each step whether there are *any* plots
#     that satisfy the given filters, and continue or cut off that path in the call graph.
#   - and, i can use this function at the bottom level of the call graph to get
#     a unique image filename to include in the table cell. dual purpose!
#
def get_filtered_image_paths(replacement_values, page_set):
    image_file_pattern = page_set['image_files']
    to_replace_set = set(s.replace('{', '').replace('}', '') for s in re.findall(r'\{[^\}]*\}', image_file_pattern))
    auto_replace_set = set(k for k in replacement_values.keys())
    to_manually_replace_set = to_replace_set - auto_replace_set
    for to_manually_replace in to_manually_replace_set:
        image_file_pattern = image_file_pattern.replace('{' + to_manually_replace + '}', '*')
    image_file_pattern = image_file_pattern.format(**replacement_values).replace(' ', '_')
    paths_found = glob.glob(get_dir_data(g) + '/' + image_file_pattern)

    if not len(paths_found):
        tee('image_file_pattern       ={}'.format(image_file_pattern))
        tee('to_replace_set           ={}'.format(to_replace_set))
        tee('auto_replace_set         ={}'.format(auto_replace_set))
        tee('to_manually_replace_set  ={}'.format(to_manually_replace_set))
        tee('get_filtered_image_paths ={}'.format(image_file_pattern))
        tee('glob for this pattern    ={}'.format(paths_found))

    return paths_found

def get_field_values_from_page_set(page_set, key):
    field = page_set[key]
    if isinstance(field, list):
        field_values = field
        field = key
    elif field in g['run_params']:
        field_values = g['run_params'][field]
    elif not field:
        field_values = ['']
    else:
        tee_fatal('page_set: {} must be a run parameter field name, or a list of values, or empty; it is {}'.format(key, field))

    return field, field_values

## also builds any raw data subpage linked from this cell
def get_td_contents_html(replacement_values, page_set):
    tee('get_td_contents_html: replacement_values = {}'.format(replacement_values))

    ## locate image file (and obtain corresponding text and html file names)

    plot_filename = get_filtered_image_paths(replacement_values, page_set)
    if len(plot_filename) != 1:
        tee_fatal("improperly defined page_set: one particular table cell defined by replacements:\n{}\n\nis apparently supposed to contain ZERO or MORE THAN ONE image:\n{}\n\nthis is for page_set {}\n\nif there is more than one image listed, then it is likely you forgot to specify some page/table/row/column field to restrict this cell to exactly one image. if there are zero images listed, you likely filtered incorrectly. it's also possible you spelled a field incorrectly, or added a field that doesn't exist.".format(replacement_values, plot_filename, page_set))
    assert(len(plot_filename) == 1)
    plot_filename = plot_filename[0]
    plot_filename = os.path.relpath(plot_filename, get_dir_data(g))

    plot_txt_filename = plot_filename.replace('.png', '.txt')
    plot_txt_full_filename = plot_filename.replace('.png', '_full.txt')
    html_filename = plot_filename.replace('.png', '.html')

    ## construct html for the cell

    td_contents_html = '<a href="{}"><img src="{}" border="0"></a>'.format( html_filename, plot_filename )

    ## create html summary and add it to the filename_to_html global dict...
    ##
    ## note: this isn't really the "right" place to do this from an engineering perspective,
    ##       BUT if i did this somewhere else there would be a LOT of repeated code,
    ##       as this is the only point in the code where we've already determined exactly
    ##       which pages, tables, rows, cols, images SHOULD exist in the final output...

    with open(get_dir_data(g) + '/' + plot_txt_full_filename, 'r') as f:
        data_txt_full = f.read()
    with open(get_dir_data(g) + '/' + plot_txt_filename, 'r') as f:
        data_txt = f.read()

    content_html = '<pre>{}\n\n{}</pre>'.format(data_txt_full, data_txt)
    filename_to_html[html_filename] = template_html.replace('{template_content}', content_html)

    return td_contents_html

## also builds any raw data subpages linked from this table
def get_table_html(replacement_values, page_set, table_field, table_field_value):
    tee()
    tee('get_table_html: replacement_values = {}'.format(replacement_values))

    ## if this set of replacement_values yields no actual images, prune this TABLE
    if len(get_filtered_image_paths(replacement_values, page_set)) == 0: return ''

    column_field, column_field_values = get_field_values_from_page_set(page_set, 'column_field')
    row_field, row_field_values = get_field_values_from_page_set(page_set, 'row_field')

    ## header: replacement for {template_table_th_list}
    __table_th_list = ''
    __table_th_list += template_table_th.format(table_th_text=str(row_field)) ## dummy first column so we can have row titles as well

    for cval in column_field_values:
        __table_th_list += template_table_th.format(table_th_text=(column_field+'='+str(cval)))

    ## rows: replacement for {template_table_tr_list}
    __table_tr_list = ''

    for rval in row_field_values:
        replacement_values[row_field] = str(rval)

        ## if this set of replacement_values yields no actual images, prune this ROW
        if len(get_filtered_image_paths(replacement_values, page_set)) == 0:
            tee('pruning row {} in table "{}={} in page {} under replacement_values {}"'.format(rval, table_field, table_field_value, page_set['name'], replacement_values))

        else:
            __table_tr_td_list = ''                             ## all columns in row: replacement for {template_table_tr_td_list}
            __table_tr_td = str(rval)                           ## row header
            __table_tr_td_list += template_table_tr_td.format(table_tr_td_text=__table_tr_td)

            for cval in column_field_values:
                replacement_values[column_field] = str(cval)    ## one table cell: replacement for {template_table_tr_td}

                ## if this set of replacement_values yields no actual images, prune this CELL
                if len(get_filtered_image_paths(replacement_values, page_set)) == 0:
                    tee('pruning cell {} in row {} of table "{}={} in page {} under replacement_values {}"'.format(cval, rval, table_field, table_field_value, page_set['name'], replacement_values))

                else:
                    cell_html = get_td_contents_html(replacement_values, page_set)

                    ## cval
                    __table_tr_td_list += template_table_tr_td.format(table_tr_td_text=cell_html)

                if column_field:
                    del replacement_values[column_field]

            ## rval
            __table_tr_list += template_table_tr.format(template_table_tr_td_list=__table_tr_td_list)

        if row_field:
            del replacement_values[row_field]

    return \
        template_table.format( \
            template_table_title=('' if (str(table_field) == '') else ('<h3>' + str(table_field) + ' = ' + str(table_field_value) + '</h3>')), \
            template_table_th_list=__table_th_list, \
            template_table_tr_list=__table_tr_list \
        )



## also builds any raw data pages linked from this one...
def build_html_page(replacement_values, page_set):
    tee()
    tee('build_html_page: replacement_values = {}'.format(replacement_values)) #dict( (k,g['replacements'][k]) for k in page_set['fields']) )

    ## if this set of replacement_values yields no actual images, prune this PAGE
    if len(get_filtered_image_paths(replacement_values, page_set)) == 0:
        log('build_html_page: pruning page w/replacements {}'.format(replacement_values))
        return None

    content_html = ''

    table_field, table_field_values = get_field_values_from_page_set(page_set, 'table_field')

    for table_field_value in table_field_values:
        if table_field: replacement_values[table_field] = table_field_value

        if page_set['legend_file']:
            content_html += '<img src="{}" width="100%" />'.format(page_set['legend_file'])

        content_html += get_table_html(replacement_values, page_set, table_field, table_field_value)

        if table_field: del replacement_values[table_field]

    ## add field replacements to filename as appropriate
    # print("REPLACEMENTS={}".format(replacement_values))
    # print("name={}".format(page_set['name']))
    filename = page_set['name'].format(**replacement_values)
    keys = page_set['page_field_list']
    values = [replacement_values[k] for k in keys]
    if len(values) > 0:
        assert(len(keys) == len(values))
        filename += '-' + '-'.join([(str(k).replace(' ', '_')+'_'+str(v).replace(' ', '_')) for k,v in zip(keys, values)])
    filename += '.html'
    log('build_html_page: new page filename {}'.format(filename))

    filenames_in_navigation.append(filename)
    filename_to_html[filename] = template_html.replace('{template_content}', content_html)



def build_html_page_set(i, page_set):
    tee()
    tee('page_set {}'.format(i))
    tee_pp(page_set)

    to_enumerate = dict((k, g['run_params'][k]) for k in page_set['page_field_list'])
    log('to_enumerate={}'.format(to_enumerate))
    enumerated_values = dict()
    for_all_combinations(0, to_enumerate, enumerated_values, build_html_page, page_set)



## design:
##
## all html pages are first created in memory--stored in global dict filename_to_html.
## then, they are all dumped to disk.

def do_pages(args):
    if args.pages:
        tee()
        tee_note('## Creating html pages to show plots')

        for i, page_set in zip(range(len(g['pages'])), g['pages']):
            build_html_page_set(i, page_set)

        ## build index.html from template and provided content_index_html string (if any)

        content_index_html = ''
        if 'content_index_html' in g:
            content_index_html = g['content_index_html']

        filename_to_html['index.html'] = template_html.replace('{template_content}', content_index_html)
        filenames_in_navigation.append('index.html')

        ## build output_log.html from index.html template

        with open(os.getcwd() + '/output_log.txt', 'r') as f:
            file_contents = f.read()
        filename_to_html['output_log.html'] = template_html.replace( \
            '{template_content}', \
            '<pre>\n'+file_contents+'\n</pre>' \
        )
        filenames_in_navigation.append('output_log.html')

        ## build navigation sidebar links

        tee()
        nav_link_list = ''
        for f in filenames_in_navigation:
            pretty_name = f.replace('.html', '').replace('_', ' ').replace('-', ' ')
            tee('adding "{}" a.k.a "{}" to navigation'.format(f, pretty_name))
            nav_link_list += template_nav_link.format(nav_link_href=f, nav_link_text=pretty_name)

        ## add navigation sidebar to pages

        tee()
        for i, fname in zip(range(len(filename_to_html.keys())), filename_to_html.keys()):
            html = filename_to_html[fname]
            filename_to_html[fname] = html.replace('{template_nav_link_list}', nav_link_list)
            tee('adding navigation to filename_to_html[{}]={}'.format(i, fname))
            # tee('html_summary_file = {}, html = {}'.format(fname, html_summary_files[fname]))

        ## dump html files to disk

        tee()
        for i, fname in zip(range(len(filename_to_html.keys())), filename_to_html.keys()):
            html = filename_to_html[fname]
            tee('writing {} to data dir'.format(fname))
            with open(get_dir_data(g) + '/' + fname, 'w') as f:
                f.write(html)

    # else:
    #     tee()
    #     tee('## Skipping html page creation')

#########################################################################
#### --deploy-to
#########################################################################

def do_deploy_to(args):
    if args.deploy_to:
        tee()
        tee_note('## Deploying data to cs.uwaterloo.ca/~t35brown/rift/preview/{} ...'.format(args.deploy_to[0]))
        tee(shell_to_str('cd {} ; ./deploy_dir.sh {} {}'.format(get_dir_tools(g), get_dir_data(g), args.deploy_to[0])))

#########################################################################
#### Debug: deploy_dir
#########################################################################

def do_debug_deploy(args):
    if args.debug_deploy:
        tee()
        tee_note('## Deploying data directory to linux.cs ...')
        tee(shell_to_str('cd {} ; ./deploy_dir.sh {} uharness'.format(get_dir_tools(g), get_dir_data(g))))

#########################################################################
#### Finish
#########################################################################

def do_finish(args, error_exit_code=1):
    plt.close('all')

    ended = datetime.datetime.now()
    ended_sec = int(shell_to_str('date +%s'))
    elapsed_sec = ended_sec - started_sec

    if args.zip:

        ## get working directory where ZIP should be deposited.
        cwd = os.getcwd()
        ## then find the top level directory in the SAME git repo.
        repo_root = shell_to_str('git rev-parse --show-toplevel').rstrip('\n\r ')
        cwd_rel = os.path.relpath(cwd, repo_root)

        log()
        log('## Fetching git status and any uncommitted changes for archival purposes')
        log()

        commit_hash = shell_to_str('cd '+repo_root+'; '+'git log | head -1')
        if commit_hash.startswith('commit '):
            commit_hash = commit_hash[len('commit '):]
        log('commit_hash={}'.format(commit_hash))

        git_status = shell_to_str('cd '+repo_root+'; '+'git status')
        log('git_status:')
        log(git_status)
        log()
        diff_files = shell_to_list('cd '+repo_root+'; '+'git status -s | awk \'{if ($1 != "D") print $2}\' | grep -v "/$"', sep='\n')
        log('diff_files={}'.format(diff_files))
        log()

        ## Move old ZIP file(s) into backup directory

        log(shell_to_str('if [ ! -e _output_backup ] ; then mkdir _output_backup 2>&1 ; fi'))
        log(shell_to_str('mv output_*.zip _output_backup/', ignore_error=True))

        ## ZIP results

        zip_filename = ended.strftime('output_%yy%mm%dd_%Hh%Mm%Ss').rstrip('\n\r') + '.zip' #'_' + commit_hash + '.zip'

        tee()
        tee_note('## Zipping results into ./{}'.format(zip_filename))
        log()

        data_relpath = os.path.relpath(g['replacements']['__dir_data'], repo_root).rstrip('\n\r')
        log('running: zip -r {} {} {}'.format(zip_filename, data_relpath, ' '.join(diff_files)))
        log('     in: {}'.format(repo_root))
        log_replace_and_run('cd '+repo_root+'; zip -r {} {} {}'.format(zip_filename, data_relpath, ' '.join(diff_files)))

        log('moving {} to {}'.format(repo_root+'/'+zip_filename, cwd_rel))
        log(shell_to_str('cd '+repo_root+'; mv '+zip_filename+' '+cwd_rel+'/'))

    if args.compile or args.run or args.createdb or args.plot or args.pages or args.debug_deploy or args.deploy_to:
        tee()
        tee_note('## Finish at {} (started {}); total {}'.format(ended, started, print_time(elapsed_sec)))

        if len(validation_errors) > 0:
            tee()
            tee_error('there were data validation errors!')
            tee()
            for item in validation_errors:
                tee('    ' + item)
            if error_exit_code: do_exit(code=error_exit_code)
            else: return

        if len(g['sanity_check_failures']) > 0:
            tee()
            tee_error('there were sanity check failures!')
            tee()
            for item in g['sanity_check_failures']:
                tee('    ' + item)

        # if not DISABLE_TEE_STDOUT: print()
        if not DISABLE_TEE_STDOUT: print("Note: log can be viewed at ./output_log.txt")

    if not DISABLE_LOGFILE_CLOSE: g['log'].close()

    ## add *final* log to zipfile...
    if args.zip: shell_to_str('zip -r {} output_log.txt'.format(zip_filename))

#########################################################################
#### Extra command line utilities that deviate from the normal workflow
#########################################################################

def print_query_result(sql_query):
    df = select_to_dataframe(sql_query)
    headers = list(df.columns.values)
    print(table_to_str(df, headers=headers, aligned=True))

def do_query(args):
    if args.query:
        print_query_result(args.query)

def get_failed_list(args):
    df = select_to_dataframe('select __step from failures')
    iterable = [row for index, row in df.iterrows()]
    found_steps = set([int(row[0]) for row in iterable])
    ordered = sorted(found_steps)
    ordered_s = [str(v) for v in ordered]
    return ordered_s

def do_failed_list(args, sep=' '):
    if args.failed_list:
        print(sep.join(get_failed_list(args)))

def get_failed_where(args):
    return 'WHERE CAST(__step AS INTEGER) in (' + ','.join(get_failed_list(args)) + ')'

def do_failed_where(args):
    if args.failed_where:
        print(get_failed_where(args))

def do_failed_details(args):
    if args.failed_details:
        txn = 'SELECT __step'
        first = True
        for f in g['run_params'].keys():
            if first: txn += ' ' + f
            else: txn += ', ' + f
            first = False
        txn += ' FROM failures ' + get_failed_where(args)

        # first = True
        # for f in g['run_params'].keys():
        #     if first: txn += ' order by ' + f
        #     else: txn += ', ' + f
        #     first = False
        # print(txn)
        print_query_result(txn)

def do_failed_query_cols(args):
    if args.failed_query_cols and len(args.failed_query_cols):
        txn = 'SELECT DISTINCT'
        first = True
        for f in args.failed_query_cols:
            if first: txn += ' ' + f
            else: txn += ', ' + f
            first = False
        txn += ' FROM failures ' + get_failed_where(args)

        first = True
        for f in args.failed_query_cols:
            if first: txn += ' ORDER BY ' + f
            else: txn += ', ' + f
            first = False

        print(txn)
        print_query_result(txn)

#########################################################################
#### Main function
#########################################################################

def do_all(cmdline_args_list=None, define_experiment_func=None, error_exit_code=1):
    try:
        args = do_argparse(cmdline_args_list=cmdline_args_list, define_experiment_func=define_experiment_func)
        do_compile          (args)
        do_run              (args)
        do_createdb         (args)
        do_plot             (args)
        do_pages            (args)
        do_debug_deploy     (args)
        do_deploy_to        (args)
        do_finish           (args, error_exit_code=error_exit_code)

        ## extra command line utilities that deviate from the normal workflow
        do_query            (args)
        do_failed_list      (args)
        do_failed_where     (args)
        do_failed_details   (args)
        do_failed_query_cols(args)

    ## catch benign errors properly in jupyter...
    except SystemExit as ex:
        if error_exit_code:
            sys.exit(1)

if __name__ == '__main__':
    do_all()




















































#########################################################################
#### Some special functions for use in a jupyter notebook...
#########################################################################

def disable_tee_stdout():
    global DISABLE_TEE_STDOUT
    DISABLE_TEE_STDOUT = True

def enable_tee_stdout():
    global DISABLE_TEE_STDOUT
    DISABLE_TEE_STDOUT = False

def disable_logfile_close():
    global DISABLE_LOGFILE_CLOSE
    DISABLE_LOGFILE_CLOSE = True

def get_g():
    return g

## also has capability to deal with multiple concatenated fields (i.e., field is a list)
def select_distinct_field(field, where = '', concat_sep=' ', exp_dict=None):
    if not exp_dict: exp_dict = g

    f_sql, f_name = field, field
    if (field and not isinstance(field, str)):
        f_sql, f_name = get_concat_column(field, concat_sep)
        txn = 'SELECT DISTINCT {} FROM data {}'.format(f_sql, where)
    else:
        ## perform sqlite query to fetch raw data
        txn = 'SELECT DISTINCT {} FROM data {}'.format(field, where)
    log('select_distinct_field: txn={}'.format(txn), exp_dict=exp_dict)
    cur = get_cursor(exp_dict)
    cur.execute(txn)
    data_records = cur.fetchall()
    return [x[0] for x in data_records]

def select_distinct_field_dict(field, replacement_values):
    ## perform sqlite query to fetch raw data
    txn = 'SELECT DISTINCT {} FROM data {}'.format(field, get_where_from_dict(replacement_values))
    log('select_distinct_field_dict: txn={}'.format(txn))
    cur.execute(txn)
    data_records = cur.fetchall()
    return [x[0] for x in data_records]

def get_num_rows(replacement_values):
    if len(replacement_values) == 0:
        txn = 'SELECT count({}) FROM data'.format(get_headers()[0][0])
    else:
        where = get_where_from_dict(replacement_values)
        txn = 'SELECT count({}) FROM data {}'.format(list(replacement_values.keys())[0], where)
    log('get_num_rows: txn={}'.format(txn))
    cur.execute(txn)
    data_records = cur.fetchall()
    return data_records[0][0]

def add_legend_and_reshape(g, loc='lower center', ncol=5, bottom=0.14, size=(8, 6)):
    g.add_legend(loc=loc, ncol=ncol)
    g.fig.subplots_adjust(bottom=bottom, top=1, left=0, right=1)
    g.fig.set_size_inches(size[0], size[1])

# def map_series_to_pandas_line_styles(series='', styles=['^-', 'o-', 's-', '+-', 'x-', 'v-', '*-', 'X-', '|-', '.-', 'd-'], concat_col_sep=' '):
#     if not series: return styles
#     series_distinct = select_distinct_field(series, concat_col_sep)
#     line_styles = dict()
#     for i, series in zip(range(len(series_distinct)), series_distinct):
#         line_styles[series] = styles[i % len(styles)]
#         ## ensure mapping is the same even with a leading underscore,
#         ## because we add an underscore when double plotting series
#         ## to get proper error bars... (underscore prevents duplicate legend entries)
#         line_styles['_' + series] = styles[i % len(styles)]
#     return line_styles

## markers, palette, dashes and sizes define lists of values to be used for the various series
## (sane defaults are supplied)
def get_seaborn_series_styles(series_distinct, markers=['^', 'o', 's', 'X', 'P', 'v', '*', '.', 'd'], palette=None, dashes=[''], sizes=[1], concat_col_sep=' ', plot_func=sns.lineplot, exp_dict=None):
    if not exp_dict: exp_dict = g

    ## construct mappings from each series value to round-robin choices from the above
    if isinstance(series_distinct, str): ## TODO: handle sql concatenated series fields
        series_distinct = select_distinct_field(series_distinct, concat_col_sep, exp_dict=exp_dict)
    if not palette:
        palette = sns.color_palette("colorblind", len(series_distinct))
    plot_style_kwargs = dict(palette=dict())
    for i, series in zip(range(len(series_distinct)), series_distinct):
        plot_style_kwargs['palette'][series] = palette[i % len(palette)]
        ## ensure mapping is the same even with a leading underscore,
        ## because we add an underscore when double plotting series
        ## to get proper error bars... (underscore prevents duplicate legend entries)
        plot_style_kwargs['palette']['_' + series] = palette[i % len(palette)]

    if plot_func == sns.lineplot:
        plot_style_kwargs['markers'] = dict()
        plot_style_kwargs['sizes'] = dict()
        plot_style_kwargs['dashes'] = dict()
        for i, series in zip(range(len(series_distinct)), series_distinct):
            plot_style_kwargs['markers'][series] = markers[i % len(markers)]
            plot_style_kwargs['sizes'][series] = sizes[i % len(sizes)]
            plot_style_kwargs['dashes'][series] = dashes[i % len(dashes)]
            ## ensure mapping is the same even with a leading underscore,
            ## because we add an underscore when double plotting series
            ## to get proper error bars... (underscore prevents duplicate legend entries)
            plot_style_kwargs['markers']['_' + series] = markers[i % len(markers)]
            plot_style_kwargs['sizes']['_' + series] = sizes[i % len(sizes)]
            plot_style_kwargs['dashes']['_' + series] = dashes[i % len(dashes)]

    return plot_style_kwargs

def plot_rc(row, col, series, x, y, data=None, where='', series_styles=None, plot_func=sns.lineplot, facetgrid_kwargs=dict(), plot_kwargs=dict()):
    txn = 'SELECT {}, {}, {}, {}, {} FROM DATA {}'.format(row, col, series, x, y, where)
    if data is None: data = select_to_dataframe(txn)
    #display(df)

    if 'margin_titles' not in facetgrid_kwargs.keys():
        facetgrid_kwargs['margin_titles'] = True

    g = sns.FacetGrid(data=data, row=row, col=col, **facetgrid_kwargs)

    if not series_styles:
        series_styles = get_seaborn_series_styles(series, plot_func=plot_func)

    def plot_facet(x, y, series, **kwargs):
        if plot_func == sns.lineplot: series_styles['style'] = series
        #display(x, y, series, series_styles, kwargs)
        plot_func(x=x, y=y, hue=series, **series_styles, **kwargs, **plot_kwargs)

    g.map(plot_facet, x, y, series)

    add_legend_and_reshape(g)

def filter_df_by_columns(df, column_filter=cf_any):
    ## compute a bitmap that dictates which columns should be included (not filtered out)
    headers = list(df.columns.values)
    # print(headers)
    include_col_indexes = []
    for i, col_name in zip(range(len(headers)), headers):
        col_values = list(df[headers[i]])
        # print('col_name={} col_values={}'.format(col_name, col_values))
        if column_filter(col_name, col_values):
            include_col_indexes.append(i)
    return df[ [headers[i] for i in include_col_indexes] ]

def plot_to_axes(x, y, series='', where='', kind='line', ax=None, display_data=False):
    ## TODO: run sanity checks on this plot...
    headers, records = get_records_plot(series, x, y, where)
    csv = table_to_str(records, headers)
    df = pd.read_csv(io.StringIO(csv), sep=' ')

    if display_data == 'full':
        df_full = select_to_dataframe('select * from data {}'.format(where))
        display(df_full)
    elif display_data == 'diff':
        df_full = select_to_dataframe('select * from data {}'.format(where))
        df_diff = filter_df_by_columns(df_full, column_filter=cf_and(cf_not_intrinsic, cf_not_identical))
        display(df_diff)
    elif display_data == 'diff2':
        df_full = select_to_dataframe('select * from data {}'.format(where))
        df_diff = filter_df_by_columns(df_full, column_filter= \
                cf_andl([ \
                    cf_not_in([series, x, y]), \
                    cf_run_param, \
                    cf_not_intrinsic, \
                    cf_not_identical
                ]))
        if len(df_diff.columns.values) > 0:
            display(df_diff)
    elif display_data:
        display(df)

    pivot_table_kwargs = dict()
    if series != '':
        pivot_table_kwargs['columns'] = series

    plot_kwargs = dict()
    plot_kwargs['kind'] = kind
    if ax != None:
        plot_kwargs['ax'] = ax

    table = pd.pivot_table(df, values=y, index=x, aggfunc=np.mean, **pivot_table_kwargs)
    table.plot(**plot_kwargs)

def plot_to_axes_dict(x, y, filter_values, series='', ax=None, display_data=False):
    plot_to_axes(x, y, series=series, where=get_where_from_dict(filter_values), ax=ax, display_data=display_data)

def plot_to_axes_grid(result_dict, replacement_values, series, x, y, row_field, row_values, row_index, column_field, column_values, column_index):
    # print('get_table: {}'.format(locals()))
    if 'axes' not in result_dict.keys():
        fig, axes = plt.subplots(nrows=len(row_values), ncols=len(column_values))
        result_dict['axes'] = axes
        result_dict['fig'] = fig

    ## plot in one axis grid cell
    where = get_where_from_dict(replacement_values)
    headers, records = get_records_plot(series, x, y, where)
    csv = table_to_str(records, headers)
    df = pd.read_csv(io.StringIO(csv), sep=' ')
    table = pd.pivot_table(df, values=y, index=x, columns=series, aggfunc=np.mean)
    table.plot(ax=result_dict['axes'][row_index,column_index])

## calls func(func_arg, replacement_values, series_field, x_field, y_field, column_field, column_values, column_index, row_field, row_values, row_index)
def table_of_plots_lambda(replacement_values, series_field, x_field, y_field, row_field, column_field, func, func_arg):
    tee()
    tee('table_of_plots_lambda: replacement_values = {}'.format(replacement_values))

    ## if this set of replacement_values yields no actual data, prune this TABLE
    if get_num_rows(replacement_values) == 0:
        tee('pruning table under replacement_values {}"'.format(replacement_values))
        return

    # column_field = page_set['column_field']
    column_field_values = [''] if (column_field == '') else select_distinct_field_dict(column_field, replacement_values) #g['run_params'][column_field]
    # row_field = page_set['row_field']
    row_field_values = [''] if (row_field == '') else select_distinct_field_dict(row_field, replacement_values) #g['run_params'][row_field]

    for rix, rval in zip(range(len(row_field_values)), row_field_values):
        replacement_values[row_field] = str(rval)

        if get_num_rows(replacement_values) == 0:          ## if this set of replacement_values yields no actual data, prune this ROW
            tee('pruning row {} under replacement_values {}"'.format(rval, replacement_values))
        else:
            for cix, cval in zip(range(len(column_field_values)), column_field_values):
                replacement_values[column_field] = str(cval)

                if get_num_rows(replacement_values) == 0:  ## if this set of replacement_values yields no actual images, prune this CELL
                    tee('pruning cell {} in row {} under replacement_values {}"'.format(cval, rval, replacement_values))
                else:
                    func(func_arg, replacement_values, series_field, x_field, y_field, row_field, row_field_values, rix, column_field, column_field_values, cix)

                del replacement_values[column_field]

        del replacement_values[row_field]

def sanity_check_aggregation(y, x='', series='', where=''):
    exclude_cols = []
    if series:
        if '__AND__' in series:
            sub_cols = series.replace('__AND__', '|').split('|')
            #print(sub_cols)
            exclude_cols.extend(sub_cols)
        else: exclude_cols.append(series)
    if x:
        if '__AND__' in x:
            sub_cols = x.replace('__AND__', '|').split('|')
            #print(sub_cols)
            exclude_cols.extend(sub_cols)
        else: exclude_cols.append(x)
    exclude_cols.append(y)

    df_full = select_to_dataframe('select * from data {}'.format(where))
    df_diff = filter_df_by_columns(df_full, column_filter= \
            cf_andl([ \
                cf_not_in(exclude_cols), \
                cf_run_param, \
                cf_not_intrinsic, \
                cf_not_identical
            ]))
    if len(df_diff.columns.values) > 0:
        print('WARNING: possibly aggregating values inappropriately over these column values...')
        display(df_diff)
        print('NOTE: if this is not a mistake, you can likely suppress this check by specifying argument sanity_check=False in the calling function')
        assert(False)

def get_concat_column(column_list, sep=' '):
    assert(not isinstance(column_list, str))
    assert(isinstance(column_list, list))
    assert(len(column_list) > 0)
    assert(isinstance(column_list[0], str))
    concat_name = ''
    column_sql = ''
    for c in column_list:
        assert(' ' not in c)
        concat_name += ('__AND__' if concat_name else '') + str(c)
        column_sql += ('||"{}"||'.format(sep) if column_sql else '') + str(c)
    column_sql += ' AS ' + concat_name
    return column_sql, concat_name

## note: plot_sql passes all keyword arguments to plot_func
def get_dataframe_and_call(plot_func, where, y, x='', series='', sanity_check=True, concat_col_sep=' ', **kwargs):

    ## if series or x are LISTS of columns,
    ## produce the necessary SQL to concatenate them...
    s_sql, s_name = series, series
    if (series and not isinstance(series, str)):
        s_sql, s_name = get_concat_column(series, sep=concat_col_sep)

    x_sql, x_name = x, x
    if (x and not isinstance(x, str)):
        x_sql, x_name = get_concat_column(x, sep=concat_col_sep)

    columns_sql = ''
    if s_sql: columns_sql += (', ' if columns_sql else '') + s_sql
    if x_sql: columns_sql += (', ' if columns_sql else '') + x_sql
    columns_sql += (', ' if columns_sql else '') + y

    ## query the data and sanity check it
    df = select_to_dataframe('select {} from DATA {}'.format(columns_sql, where))
    # display(df)
    if sanity_check:
        sanity_check_aggregation(y=y, x=x_name, series=s_name, where=where)

    ## call the plot function, relaying all keyword args
    kwargs.update(sanity_check=sanity_check, concat_col_sep=concat_col_sep) ## pass these as part of kwargs as well... just in case the user wants them...
    plot_func(data=df, y=y, x=x_name, series=s_name, **kwargs)

def show_html(fname, height=500):
    display(HTML('<div style="height: '+str(height)+'px; width: 100%; overflow-x: auto; overflow-y: hidden; resize: both"><iframe src="'+fname+'" style="width: 100%; height: 100%; border: none"></iframe></div>'))
    # display(HTML('<div style="height: 500px; width: 100%; overflow-x: auto; overflow-y: hidden; resize: both"><iframe src="'+os.getcwd()+'/'+fname+'" style="width: 100%; height: 100%; border: none"></iframe></div>'))

def show_txt(fname, height=300):
    with open(fname, 'r') as f:
        contents = f.read()
    new_fname = fname.replace('.txt', '.html')
    with open(new_fname, 'w') as f:
        f.write(template_data_html.replace('{template_content}', '<pre>'+contents+'</pre>'))
    show_html(new_fname, height)






def init_for_jupyter(user_experiment_python_file):
    disable_tee_stdout()
    disable_logfile_close()
    ## get database connection
    do_all( \
            cmdline_args_list='{}'.format(user_experiment_python_file).split(' ') \
    )

def run_in_jupyter(define_experiment_func, cmdline_args='', error_exit_code=1):
    # disable_logfile_close()
    ## get database connection
    do_all(
              cmdline_args_list=cmdline_args.split(' ')
            , define_experiment_func=define_experiment_func
            , error_exit_code=error_exit_code
    )



#display('data/index.html')
#fname=os.getcwd()+'/data/PAPI_L3_TCM.html'
#display(fname)
#display(IFrame(os.getcwd()+'/data/index.html', width=400, height=400))
#display(HTML(filename=))
#display(shell_to_str('cat ' + fname))
#display('<iframe width=400 height=400>'+shell_to_str('cat '+fname)+'</iframe>')
#display(HTML('<iframe src="'+fname+'" width=400 height=400></iframe>'))
#display(HTML('<iframe src="data/PAPI_L3_TCM.html" width=400 height=400></iframe>'))