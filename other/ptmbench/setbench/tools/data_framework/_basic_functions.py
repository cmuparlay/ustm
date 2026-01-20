#!/usr/bin/python3

import inspect
import subprocess
import os
import sys
import pprint
import datetime
import argparse
import sqlite3
import numpy
import io
import re
import glob
from colorama import Fore, Back, Style

######################################################
#### private helper functions
######################################################

def shell_to_str(cmd, exit_on_error=False, ignore_error=False, print_error=True):
    child = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, shell=True)
    returncode = child.returncode
    if returncode != 0 and not ignore_error:
        if int(returncode) == 124:
            ## timeout
            if print_error: print(Style.BRIGHT+Fore.RED+"## warning: timeout command killed the process"+Style.RESET_ALL)
            return int(returncode)
        if print_error:
            print()
            print(Back.RED+Fore.BLACK+'########################################################'+Style.RESET_ALL)
            print(Back.RED+Fore.BLACK+'#### ERROR OUTPUT:'+Style.RESET_ALL)
            print(Back.RED+Fore.BLACK+'########################################################'+Style.RESET_ALL)
            if child.stderr != None:
                ret_stderr = child.stderr.decode('utf-8').rstrip(' \r\n')
                print()
                print(ret_stderr)
                print()
                print(child.stdout.decode('utf-8').rstrip(' \r\n'))
                print()
            print('ERROR: running "{}" ; returncode {}'.format(cmd, returncode))

        if exit_on_error: sys.exit(1)
        else: return int(returncode)

    ret_stdout = child.stdout.decode('utf-8').rstrip(' \r\n')
    return ret_stdout

def shell_to_list(cmd, sep=' ', exit_on_error=False, print_error=True):
    return shell_to_str(cmd, exit_on_error, print_error).split(sep)

def shell_to_listi(cmd, sep=' ', exit_on_error=False, print_error=True):
    return [int(x) for x in shell_to_str(cmd, exit_on_error, print_error).split(sep)]

def type_invariant_equals(x, y):
    if isinstance(x, float) and isinstance(y, float):
        return abs(x - y) < 1e-6

    if type(x) == type(y): return x == y

    if isinstance(x, float) or isinstance(y, float):
        ## one is float and other isn't... try to cast the other
        fail_cast=False
        try:
            if isinstance(x, float):
                other = float(y)
                if abs(x - y) < 1e-6: return True
            if isinstance(y, float):
                other = int(x)
                if abs(x - y) < 1e-6: return True
        except ValueError:
            fail_cast=True ## do nothing

    if isinstance(x, int) or isinstance(y, int):
        ## one is int and other isn't... try to cast the other
        fail_cast=False
        try:
            if isinstance(x, int):
                other = int(y)
                if x == y: return True
            if isinstance(y, int):
                other = int(x)
                if x == y: return True
        except ValueError:
            fail_cast=True ## do nothing

    return str(x) == str(y)

######################################################
#### validator functions
######################################################

def is_optional(exp_dict, value):
    return True

def is_nonempty(exp_dict, value):
    return str(value) != '' and str(value) != 'None' and value != None

def is_positive(exp_dict, value, eps=1e-6):
    try:
        return float(value) > eps
    except ValueError: return False

## note: this is not a validator per se, but rather RETURNS a validator (obtained via currying an argument)
def is_equal(to_this_value):
    def fn(exp_dict, value):
        return value == to_this_value
    return fn

## note: this is not a validator per se, but rather RETURNS a validator (obtained via currying an argument)
def is_run_param(param_name):
    def fn(exp_dict, value):
        for other in exp_dict['run_params'][param_name]:
            if type_invariant_equals(value, other): return True
        return False
    return fn

## memoize files into data to make grepping efficient
def grep_all_lines_memoize(exp_dict, file_name, memo_dict_name):
    if memo_dict_name not in exp_dict: exp_dict[memo_dict_name] = dict()
    if file_name not in exp_dict[memo_dict_name].keys():
        data_list = shell_to_list('grep -E "^[^ =]+=" {}'.format(file_name), sep='\n', exit_on_error=True)
        # print("data_list=" + repr(data_list))
        newdict = dict()
        for item in data_list:
            # print('splitting "{}"'.format(item))
            tokens = item.split('=')
            # if len(tokens) == 2:
            #     newdict[tokens[0]] = tokens[1]
            # else:
            #     newdict[tokens[0]] = '='.join(tokens[1:])
            newdict[tokens[0]] = '='.join(tokens[1:])
            # print('     into "{}" and "{}"'.format(tokens[0], '='.join(tokens[1:])))
        exp_dict[memo_dict_name][file_name] = newdict

######################################################
#### extractor functions
######################################################

def grep_line(exp_dict, file_name, field_name):   ## memoize files into data to make grepping efficient
    grep_all_lines_memoize(exp_dict, file_name, 'memo_after_run')
    if field_name in exp_dict['memo_after_run'][file_name]:
        return exp_dict['memo_after_run'][file_name][field_name]
    else:
        return 0

def run_param(exp_dict, file_name, field_name):
    #print('run_param({}, {}) returns {}'.format(file_name, field_name, exp_dict['replacements'][field_name]))
    return exp_dict['replacements'][field_name]

######################################################
#### private functions
######################################################

def get_run_param(exp_dict, field_name):
    return exp_dict['run_params'][field_name]

def is_in_grep_results(exp_dict, file_name, field_name):
    grep_all_lines_memoize(exp_dict, file_name, 'memo_before_run')
    if field_name in exp_dict['memo_before_run'][file_name]: return True
    return False

def direct_grep_file(file_name, field_name):
    return shell_to_str('grep -E "^{field_name}+=" {file_name}'.format(**locals()), ignore_error=True)


#########################################################################
#### Some extra utility functions for user experiment definition files
#########################################################################

## test whether the OS has time utility, and whether it has the desired capabilities
def os_has_suitable_time_cmd(time_cmd):
    s = shell_to_str(time_cmd + ' echo 2>&1', print_error=False)
    if isinstance(s, int):
        assert(s != 0)                          ## error code was returned (so it's non-zero...)
        assert(s != 124)                        ## not timeout command killing us
    else:
        assert(isinstance(s, str))
        if s.count(',') == time_cmd.count(','):      ## appropriate number of fields emitted by time util
            return True
    return False

def get_best_typecast(value):
    try: result = int(value)
    except ValueError:
        try: result = float(value)
        except ValueError: result = str(value)
    return result

## given a list of strings of form "X=Y",
#  find "field_name=..." and return the "...",
#  AFTER casting to the most appropriate type in {int,float,str}
def get_field_from_kvlist(kvlist, field_name):
    if any([not item or item.count('=') != 1 for item in kvlist]):
        print(Back.RED+Fore.BLACK+'## ERROR: some item is not of the form "ABC=XYZ" in list {}'.format(kvlist)+Style.RESET_ALL)
        return ''

    if not any ([item.split('=')[0] == field_name for item in kvlist]):
        print(Back.RED+Fore.BLACK+'## ERROR: field_name {} not found in list {}'.format(field_name, kvlist)+Style.RESET_ALL)
        return ''

    for item in kvlist:
        k,v = item.split('=')
        if k == field_name: value = v

    return get_best_typecast(value)

def extract_time_subfield(exp_dict, file_name, field_name):
    kvlist = shell_to_list('grep -E "^\[time_cmd_output\]" {} | tail -1 | cut -d" " -f2- | tr -d " "'.format(file_name), sep=',')
    return get_field_from_kvlist(kvlist, field_name)

def extract_time_percent_cpu(exp_dict, file_name, field_name):
    kvlist = shell_to_list('grep -E "^\[time_cmd_output\]" {} | tail -1 | cut -d" " -f2- | tr -d " "'.format(file_name), sep=',')
    return get_field_from_kvlist(kvlist, field_name).replace('%', '')

######################################################
#### user facing functions
######################################################

def set_dir_compile(exp_dict, s):
    exp_dict['replacements']['__dir_compile'] = s

def set_dir_tools(exp_dict, s):
    exp_dict['replacements']['__dir_tools'] = s

def set_dir_run(exp_dict, s):
    exp_dict['replacements']['__dir_run'] = s

def set_dir_data(exp_dict, s):
    exp_dict['replacements']['__dir_data'] = s

def set_cmd_compile(exp_dict, s):
    exp_dict['cmd_compile'] = s

def set_cmd_run(exp_dict, s):
    if 'timeout' in s:
        add_data_field(exp_dict, 'timeout', coltype='TEXT', validator=is_equal('false'))

    exp_dict['time_fields'] = []
    if '{__time_cmd}' in s:
        time_cmd = '/usr/bin/time -f "[time_cmd_output] time_elapsed_sec=%e, faults_major=%F, faults_minor=%R, mem_maxresident_kb=%M, user_cputime=%U, sys_cputime=%S, percent_cpu=%P"'
        if os_has_suitable_time_cmd(time_cmd):

            s = s.replace('{__time_cmd}', time_cmd)

            ## add data fields for time command
            add_data_field  (exp_dict, 'time_elapsed_sec'   , coltype='REAL', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'faults_major'       , coltype='INTEGER', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'faults_minor'       , coltype='INTEGER', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'mem_maxresident_kb' , coltype='INTEGER', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'user_cputime'       , coltype='REAL', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'sys_cputime'        , coltype='REAL', extractor=extract_time_subfield)
            add_data_field  (exp_dict, 'percent_cpu'        , coltype='INTEGER', extractor=extract_time_percent_cpu)
            exp_dict['time_fields'] = ['time_elapsed_sec', 'faults_major', 'faults_minor', 'mem_maxresident_kb', 'user_cputime', 'sys_cputime', 'percent_cpu']
        else:
            s = s.replace('{__time_cmd}', '')

    exp_dict['cmd_run'] = s.replace('\n', '').replace('\r', '') \
            .replace('    ', ' ').replace('    ', ' ').replace('    ', ' ').replace('    ', ' ') \
            .replace('  ', ' ').replace('  ', ' ').replace('  ', ' ').replace('  ', ' ')

def get_time_fields(exp_dict):
    return exp_dict['time_fields']

def set_file_data(exp_dict, s):
    exp_dict['file_data'] = s

def get_file_data(exp_dict):
    return exp_dict['file_data']

def get_dir_compile(exp_dict):
    return exp_dict['replacements']['__dir_compile']

def get_dir_tools(exp_dict):
    return exp_dict['replacements']['__dir_tools']

def get_dir_run(exp_dict):
    return exp_dict['replacements']['__dir_run']

def get_dir_data(exp_dict):
    return exp_dict['replacements']['__dir_data']

def get_data_fields(exp_dict):
    return list(exp_dict['data_fields'].keys())

def get_numeric_data_fields(exp_dict):
    return list(filter( \
        lambda x: exp_dict['data_fields'][x]['coltype'] in ['INTEGER', 'REAL'] \
                  and x not in exp_dict['run_params'], \
        exp_dict['data_fields'].keys()))

def add_data_field(exp_dict, name, coltype='TEXT', validator=is_nonempty, extractor=grep_line):
    d = locals()
    del d['exp_dict']
    exp_dict['data_fields'][name] = d
    # exp_dict['data_fields'][name] = dict({ 'name':name, 'coltype':coltype, 'validator':validator, 'extractor':extractor })

def add_run_param(exp_dict, name, value_list, add_as_data_field=True, filter=None):
    # print('{}('{}', {})'.format(inspect.currentframe().f_code.co_name, name, value_list))
    exp_dict['run_params'][name] = value_list
    exp_dict['run_params_filters'][name] = filter
    if add_as_data_field:
        ## infer column type from value_list
        seen_str = False
        seen_int = False
        seen_float = False
        for v in value_list:
            if isinstance(v, str): seen_str = True
            elif isinstance(v, int): seen_int = True
            elif isinstance(v, float): seen_float = True
            else:
                print(Back.RED+Fore.BLACK+'## ERROR: type {} of value {} of param {} not handled'.format(type(v), v, name)+Style.RESET_ALL)
                assert(False)
        ctype = 'TEXT' if seen_str else ('REAL' if seen_float else 'INTEGER')
        #print('add_run_param: name={} ctype={}'.format(name, ctype))
        add_data_field(exp_dict, name, coltype=ctype, validator=is_run_param(name))

def add_plot_set(exp_dict, name, x_axis, y_axis, plot_type, series='', varying_cols_list=[], filter='', title='', plot_cmd_args='', plot_style_hooks_file=''):
    if plot_style_hooks_file == '' and 'plot_style_hooks_file' in exp_dict:
        plot_style_hooks_file = exp_dict['plot_style_hooks_file']
    d = locals()
    del d['exp_dict']
    exp_dict['plots'].append(d)
    # exp_dict['plots'].append(dict({ 'name':name, 'x_axis':x_axis, 'y_axis':y_axis, 'plot_type':plot_type, 'series':series, 'varying_cols_list':varying_cols_list, 'filter':filter, 'title':title, 'plot_cmd_args':plot_cmd_args, 'plot_style_hooks_file':plot_style_hooks_file }))

## if name is not defined, then we will try to infer the values of all other optional fields.
## if name is defined, then we will NOT try to infer the values of other fields.
##
## if either row_field or column_field is a list of values (rather than a field name),
## then those values will be used as replacements for the tokens {row_field} or
## {column_field}, resp., in image_files / name.
##
## this allows, for example, comparing results in numerous fields in rows,
## versus some field type in columns.
##
def add_page_set(exp_dict, image_files, name='', column_field='', row_field='', table_field='', page_field_list=[], sep='-', legend_file=''):
    tokens = image_files.split(sep)

    field_singleton_list = ''
    if name == '':
        assert(len(tokens) > 0)
        name = tokens[0]
        if column_field == '' and len(tokens) > 1:
            field_singleton_list = re.findall(r'\{[^\}]*\}', tokens[1])
            assert(len(field_singleton_list) == 1)
            column_field = field_singleton_list[0].replace('{', '').replace('}', '')
        if row_field == '' and len(tokens) > 2:
            field_singleton_list = re.findall(r'\{[^\}]*\}', tokens[2])
            assert(len(field_singleton_list) == 1)
            row_field = field_singleton_list[0].replace('{', '').replace('}', '')
        if table_field == '' and len(tokens) > 3:
            field_singleton_list = re.findall(r'\{[^\}]*\}', tokens[3])
            assert(len(field_singleton_list) == 1)
            table_field = field_singleton_list[0].replace('{', '').replace('}', '')
        if page_field_list == [] and len(tokens) > 4:
            page_field_list = re.findall(r'\{[^\}]*\}', sep.join(tokens[4:]))
            for i, f in zip(range(len(page_field_list)), page_field_list):
                page_field_list[i] = f.replace('{', '').replace('}', '')

    fields = re.findall(r'\{[^\}]*\}', image_files)
    for i, f in zip(range(len(fields)), fields):
        fields[i] = f.replace('{', '').replace('}', '')

    i=0 ; f='' ## prevent del from crashing when loops don't execute
    del i
    del f
    del field_singleton_list
    del tokens
    d = locals()
    del d['exp_dict']
    exp_dict['pages'].append(d)

def set_content_index_html(exp_dict, content_html_string):
    exp_dict['content_index_html']=content_html_string

def set_plot_style_hooks_file(exp_dict, path):
    exp_dict['plot_style_hooks_file'] = path
