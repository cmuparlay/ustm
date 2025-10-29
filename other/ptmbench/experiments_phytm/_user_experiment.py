#!/usr/bin/python3
from _basic_functions import *
import copy
from plot_style2 import *


color_list = [
      '#df7126' # 0
    , '#37946e' # 1
    , '#FFFFFF' # 2
    , '#76428a' # 3
    , '#ac3232' # 4
    , '#fbf236' # 5
    , '#b0aca0' # 6
    , '#9e0b00' # 7
    , '#000000' # 8
    , '#fbf236' # 9
    , '#2d4bad' # 10
    , '#196583' # 11
    , '#FFFFFF' #12
    , '#076794' #13
    , '#9db2bd' #14
]

unfiltered_algs = dict({
         ''                                 : dict(name=''           , use=False , hatch=''   , color=''              , line_style=''             , line_width=''     , marker_style=''       , marker_size=''        )
        # # , 'BTree.none.ptm_no_htm'                                          : dict(name='BTree-PTM'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'BTree.none.ptm_no_htm_strong_prog'                              : dict(name='BTree-PTM-strong-prog'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'BTree.none.phytm1'                                              : dict(name='HyTrin-GLock'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'BTree.none.phytm1_fgl'                                          : dict(name='HyTrin-FGL-WP'                                 , use=True  , hatch=''    , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'BTree.none.phytm1_fgl_colocated_locks'                              : dict(name='HyTrin-FGL-CL-WP'                , use=True  , hatch='/'    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'BTree.none.phytm1_fgl_wrset_sorting'                            : dict(name='BTree-Phytm1-fgl-sorting'                               , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'BTree.none.phytm1_fgl_always_sorting'                           : dict(name='BTree-Phytm1-fgl-always-sorting'                  , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'BTree.none.phytm1_fgl_always_sorting_strong_progressive'        : dict(name='BTree-Phytm1-fgl-always-sorting-strong-prog'      , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'BTree.none.phytm1_fgl_strong_progressive'                       : dict(name='HyTrin-FGL-SP'                     , use=True  , hatch=''    , color=color_list[7]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'BTree_trinity.none.trinity_quadra'                              : dict(name='Trinity'                                          , use=True  , hatch=''    , color=color_list[8]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')

        # , 'ziptree.none.phytm1'                                              : dict(name='ziptree-Phytm1-glock'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'ziptree.none.phytm1_fgl'                                          : dict(name='ziptree-Phytm1-fgl'                                 , use=True  , hatch=''    , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'ziptree.none.phytm1_fgl_strong_progressive'                       : dict(name='ziptree-Phytm1-fgl-strong-prog'                     , use=True  , hatch=''    , color=color_list[7]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'ziptree.none.trinity_quadra'                              : dict(name='ziptree-Trinity'                                          , use=True  , hatch=''    , color=color_list[8]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')


        # , 'abtree_spht.out'                                   : dict(name='SPHT'                                    , use=True  , hatch=''    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #     # , 'abtree.none.ptm_no_htm'                                          : dict(name='abtree-PTM'                               , use=True  , hatch=''    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #     # , 'abtree.none.ptm_no_htm_strong_prog'                              : dict(name='abtree-PTM-strong-prog'                               , use=True  , hatch=''    , color=color_list[11]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #     # , 'abtree.none.phytm1'                                              : dict(name='HyTrin-GLock'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #   , 'abtree.none.phytm1_fgl'                                          : dict(name='NV-HALT'                                 , use=True  , hatch=''    , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
      , 'abtree.none.phytm1_fgl_colocated_locks'                          : dict(name='NV-HALT-CL'                           , use=True  , hatch=''    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
      , 'abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence'         : dict(name='NV-HALT-CL-NOOPF'                           , use=True  , hatch='/'    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # #    , 'abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence'         : dict(name='NO-FLUSH-FENCE'          , use=True  , hatch='/'    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # #    , 'abtree.none.phytm1_fgl_colocated_locks_no_pmem'                  : dict(name='NO-NVRAM'                  , use=True  , hatch='*'    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # #    , 'abtree.none.phytm1_fgl_colocated_locks_no_phtm'                  : dict(name='NO-PERSISTENT-HTxn'                  , use=True  , hatch='o'    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #   , 'abtree.none.phytm1_fgl_colocated_locks.free_disabled'                          : dict(name='HyTrin-FGL-CL-WP-NO-FREE'                           , use=True  , hatch='/'    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #    , 'abtree.none.phytm1_fgl_colocated_locks_noop_flush_fence.free_disabled'         : dict(name='HyTrin-FGL-CL-WP-NOOP-FLUSH-FENCE-NO-FREE'          , use=True  , hatch='/'    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #    , 'abtree.none.phytm1_fgl_colocated_locks_no_pmem.free_disabled'                  : dict(name='HyTrin-FGL-CL-WP-NO-NVRAM-NO-FREE'                  , use=True  , hatch='/'    , color=color_list[0]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #    , 'abtree.none.phytm1_fgl_colocated_locks_no_phtm.free_disabled'                  : dict(name='HyTrin-FGL-CL-WP-FULLY-VOL-NO-FREE'                  , use=True  , hatch='/'    , color=color_list[0]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #     # , 'abtree.none.phytm1_fgl_wrset_sorting'                            : dict(name='abtree-Phytm1-fgl-sorting'                         , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # # #     # , 'abtree.none.phytm1_fgl_always_sorting'                           : dict(name='abtree-Phytm1-fgl-always-sorting'                  , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # #     # , 'abtree.none.phytm1_fgl_always_sorting_strong_progressive'        : dict(name='abtree-Phytm1-fgl-always-sorting-strong-prog'      , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #   , 'abtree.none.phytm1_fgl_strong_progressive'                       : dict(name='NV-HALT-SP'                     , use=True  , hatch=''    , color=color_list[7]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
      , 'abtree.none.trinity_quadra'                                      : dict(name='Trinity'                                    , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')      
       , 'abtree.none.trinity_quadra.noop_flush'                     : dict(name='Trinity-NOOPF'                                    , use=True  , hatch='/'    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # # # # #    , 'abtree_no_pmem.none.trinity_quadra'                              : dict(name='Trinity-NO-PMEM'                                    , use=True  , hatch=''    , color=color_list[8]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
       , 'abtree.none.spht'                                   : dict(name='SPHT'                                    , use=True  , hatch=''    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
       , 'abtree.none.spht_noopf'                             : dict(name='SPHT-NOOPF'                                    , use=True  , hatch='/'    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #    , 'abtree.none.spht_no_pmem'                           : dict(name='SPHT-NO-PMEM'                                    , use=True  , hatch='*'    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #    , 'abtree.none.spht_no_phtm'                           : dict(name='SPHT-NO-PHTX'                                    , use=True  , hatch='o'    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    #    # , 'brown_abtree_tm_auto.none.hytm3'                    : dict(name='Hytm3'                                    , use=True  , hatch=''    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #     , 'abtree.none.phytm1.free_disabled'                                              : dict(name='HyTrin-GLock-NO-FREE'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #    , 'abtree.none.phytm1_fgl.free_disabled'                                          : dict(name='HyTrin-FGL-WP-NO-FREE'                                 , use=True  , hatch=''    , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #    , 'abtree.none.phytm1_fgl_colocated_locks.free_disabled'                          : dict(name='HyTrin-FGL-CL-WP-NO-FREE'                           , use=True  , hatch='/'    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
    # #    , 'abtree.none.phytm1_fgl_strong_progressive.free_disabled'                       : dict(name='HyTrin-FGL-SP-NO-FREE'                     , use=True  , hatch=''    , color=color_list[7]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')

        # # , 'hashmap.none.ptm_no_htm'                                          : dict(name='abtree-PTM'                               , use=True  , hatch=''    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'hashmap.none.ptm_no_htm_strong_prog'                              : dict(name='abtree-PTM-strong-prog'                               , use=True  , hatch=''    , color=color_list[11]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # , 'hashmap.none.phytm1'                                              : dict(name='HyTrin-GLock'                               , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'hashmap.none.phytm1_fgl'                                          : dict(name='NV-HALT'         , use=True  , hatch=''    , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'hashmap.none.phytm1_fgl_colocated_locks'                          : dict(name='NV-HALT-CL'      , use=True  , hatch=''    , color=color_list[10]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # # # , 'hashmap.none.phytm1_fgl_wrset_sorting'                            : dict(name='hashmap-Phytm1-fgl-sorting'                         , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # # # , 'hashmap.none.phytm1_fgl_always_sorting'                           : dict(name='hashmap-Phytm1-fgl-always-sorting'                  , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # # # # , 'hashmap.none.phytm1_fgl_always_sorting_strong_progressive'        : dict(name='hashmap-Phytm1-fgl-always-sorting-strong-prog'      , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'hashmap.none.phytm1_fgl_strong_progressive'                       : dict(name='NV-HALT-SP'      , use=True  , hatch=''    , color=color_list[7]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'hashmap.none.trinity_quadra'                                      : dict(name='Trinity'         , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'hashmap.none.spht'                                                : dict(name='SPHT'            , use=True  , hatch=''    , color=color_list[9]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
})


host = shell_to_str('hostname').rstrip('\r\n ')

short_to_long = dict()
for k,v in unfiltered_algs.items():
    if k and v and v['name']:
        short_to_long[v['name']] = k

filtered_algs = dict()
for k,v in unfiltered_algs.items():
    if k and v and v['name'] and v['use']:
        filtered_algs[k] = v['name']

def extract_filtered_algs(exp_dict, file_name, field_name):
    result = grep_line(exp_dict, file_name, 'algorithm')
    if result in filtered_algs.keys():
        return filtered_algs[result]
    return ''

def extract_elapsed_millis(exp_dict, file_name, field_name):
    return get_best_typecast(shell_to_str('cat {} | grep "elapsed milliseconds" | cut -d":" -f2 | tr -d " "'.format(file_name)))

def extract_possibly_empty_int(exp_dict, file_name, field_name):
    numStr = shell_to_str('cat {} | grep "{}" | cut -d"=" -f2 | tr -d " "'.format(file_name, field_name))
    if numStr == "":
        return int(0)
    return int(numStr)

def extract_possibly_empty_float(exp_dict, file_name, field_name):
    numStr = shell_to_str('cat {} | grep "{}" | cut -d"=" -f2 | tr -d " "'.format(file_name, field_name))
    if numStr == "":
        return float(0)
    return float(numStr)

def extract_joules(exp_dict, file_name, field_name):    
    with open(file_name, 'r') as f:
        content = f.read()

    match = re.search(r'([\d,]+\.\d+)\s+Joules\s+power/energy-pkg/', content)
    if match:
        print(float(match.group(1).replace(',', '')))
        return float(match.group(1).replace(',', ''))
    else:
        match = re.search(r'([\d,]+\.\d+)\s+Joules\s+power/energy-ram/', content)
        if match:
            print(float(match.group(1).replace(',', '')))
            return float(match.group(1).replace(',', ''))
    return 0

## reindex styles by short alg and style property name
short_to_style_props = dict()
for prop in list(list(unfiltered_algs.values())[0].keys()):
    short_to_style_props[prop] = dict({short_name: unfiltered_algs[short_to_long[short_name]][prop] for short_name in short_to_long.keys()})


## beware: should copy.deepcopy() this to use it
base_config = dict(
      dummy = None
    , dark_mode = False
    , legend_columns = 3
    , height_inches = 4
    , hatch_map = short_to_style_props['hatch']
    , color_map = short_to_style_props['color']
    , marker_style_map = short_to_style_props['marker_style']
    , line_style_map = short_to_style_props['line_style']
    , legend_markerscale = 3
    , font_size = 24
    , xtitle = None
)

def define_experiment(exp_dict, args):
    set_dir_compile  (exp_dict, os.getcwd() + '/../')
    set_dir_tools    (exp_dict, os.getcwd() + '/../setbench/tools')
    set_dir_run      (exp_dict, os.getcwd() + '/../bin')
    set_dir_data     (exp_dict, os.getcwd() + '/data')
    cmd_compile = '../compile.sh'

    add_run_param    (exp_dict, 'algorithm'         , list(filtered_algs.keys()))

    # add_run_param    (exp_dict, 'RQ_THREADS'      , [1, 2, 4])    

    add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['0.5 0.5', '5.0 5.0', '25.0 25.0', '50.0 50.0'])    
    # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['5.0 5.0', '25.0 25.0'])    
    
    # add_run_param    (exp_dict, 'MAXKEY'            , [1000000, 2000000, 5000000])
    # add_run_param    (exp_dict, 'MAXKEY'            , [2000000, 5000000])
    add_run_param    (exp_dict, 'MAXKEY'            , [1000000])

    # pinning to node 1 then node 2
    add_run_param    (exp_dict, 'thread_pinning'    , ['-pin ' + shell_to_str('cd ' + get_dir_tools(exp_dict) + ' ; ./get_pinning_cluster.sh', exit_on_error=True)])

    # pinning to node 2 first then node 1
    # add_run_param    (exp_dict, 'thread_pinning'    , ['-pin 24-47,72-95,0-23,48-71'])

    # pinning to node 2 half way then node 1 halfway then node 2 then node 1
    # add_run_param    (exp_dict, 'thread_pinning'    , ['-pin 24-47,48-71,72-95,0-23'])

    add_run_param    (exp_dict, 'millis'            , [20000])
    # add_run_param    (exp_dict, 'millis'            , [1000])


    add_run_param    (exp_dict, 'WORK_THREADS'     , [12, 24, 36, 48, 54, 72, 96])

    add_run_param    (exp_dict, '__trials'          , [1, 2, 3, 4, 5])
    # add_run_param    (exp_dict, '__trials'          , [1])
    # add_run_param    (exp_dict, '__trials'          , list(range(1, 2000)))


    # add_run_param    (exp_dict, 'zipf'          , ["0.1", "0.9"])

    # cmd_run = 'LD_PRELOAD=../setbench/lib/libmimalloc.so timeout 360 numactl --interleave=all time  ./{algorithm} -nwork {WORK_THREADS} -nprefill {WORK_THREADS} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'
    # cmd_run = 'perf stat -e power/energy-ram/ timeout 360 numactl --interleave=all time  ./{algorithm} -nwork {WORK_THREADS} -nprefill {WORK_THREADS} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'
    cmd_run = 'timeout 360 numactl --interleave=all time  ./{algorithm} -nwork {WORK_THREADS} -nprefill {WORK_THREADS} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'
    # cmd_run = 'timeout 360 numactl --interleave=all time  ./{algorithm} -nwork {WORK_THREADS} -nprefill {WORK_THREADS} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} -dist-zipf {zipf} {thread_pinning}'
    #force prefill thread count to 1
    # cmd_run = 'numactl --interleave=all time  ./{algorithm} -nwork {WORK_THREADS} -nprefill 1 -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'

    set_cmd_compile  (exp_dict, cmd_compile)
    set_cmd_run      (exp_dict, cmd_run) ## also adds data_fields to capture the outputs of the `time` command

    add_data_field   (exp_dict, 'alg'               , coltype='TEXT', extractor=extract_filtered_algs)
    add_data_field   (exp_dict, 'total_throughput'  , coltype='INTEGER', validator=is_positive)
    add_data_field   (exp_dict, 'total_throughput_with_replay', coltype='INTEGER', extractor=extract_possibly_empty_int)
    # add_data_field   (exp_dict, 'fallback_path_time', coltype='REAL', extractor=extract_possibly_empty_float)
    add_data_field   (exp_dict, 'rq_throughput'  , coltype='INTEGER')
    add_data_field   (exp_dict, 'rq_throughput_with_replay'  , coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'TOTAL_THREADS'  , coltype='INTEGER')
    add_data_field   (exp_dict, 'PAPI_L3_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_L2_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_CYC'      , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_INS'      , coltype='REAL')
    add_data_field   (exp_dict, 'elapsed_millis'    , extractor=extract_elapsed_millis, validator=is_positive)
    add_data_field   (exp_dict, 'MILLIS_TO_RUN'     , coltype='TEXT', validator=is_positive)


    add_data_field   (exp_dict, 'sum_stm_commit_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_stm_abort_total', coltype='INTEGER', extractor=extract_possibly_empty_int)

    add_data_field   (exp_dict, 'sum_htm_commit_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_htm_abort_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'fast_htm_abort_unexplained', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_explicit', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_capacity', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_explicit_capacity', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_conflict', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_explicit_conflict', coltype='INTEGER')
    add_data_field   (exp_dict, 'fast_htm_abort_capacity_conflict', coltype='INTEGER')

    add_data_field   (exp_dict, 'joules', coltype='REAL', extractor=extract_joules)
    add_data_field   (exp_dict, 'total_ops', coltype='INTEGER')

    plotDir = '_plots/'

    alg_groups = dict()
    # alg_groups['custom-order'] = ['SPHT', 'Trinity', 'NV-HALT-SP', 'NV-HALT-CL', 'NV-HALT']
    all_algs = list(filtered_algs.values())
    alg_groups['all'] = all_algs

    # if "SPHT" in all_algs and "Trinity" in all_algs:
    #     if "SPHT" in all_algs:
    #         algs_vs_trinity = list(filtered_algs.values())
    #         algs_vs_trinity.remove("SPHT")
    #         alg_groups['halt-vs-trinity'] = algs_vs_trinity

    #     if "Trinity" in all_algs:
    #         algs_vs_spht = list(filtered_algs.values())
    #         algs_vs_spht.remove("Trinity")
    #         alg_groups['halt-vs-spht'] = algs_vs_spht

    for i, group in enumerate(alg_groups):
        # filter_sql = 'alg in ({})'.format(','.join(["'"+x+"'" for x in alg_groups[group]]))
        filter_sql = ''

        config2 = copy.deepcopy(base_config)
        config2['series_order'] = alg_groups[group]
        config2['width_inches'] = 18
        config2['height_inches'] = 12
        config2['font_size'] = 48
        config2['legend_columns'] = len(alg_groups[group])
        config2['logy'] = False


        add_plot_set(
            exp_dict
            , name=plotDir + str(group) +'_legend.png'
            , series='alg'
            # , x_axis='TOTAL_THREADS'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars_legend
            #, plot_cmd_args='--legend-only --legend-columns 3'
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            # , name=plotDir + str(group) +'_Throughput-{INS_DEL_FRAC}-key={MAXKEY}-zipf={zipf}.png'
            # , title='Throughput - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY} - zipf:{zipf}'
            # , varying_cols_list=['zipf', 'MAXKEY', 'INS_DEL_FRAC']
            , name=plotDir + str(group) +'_Throughput-{INS_DEL_FRAC}-key={MAXKEY}.png'
            , title='Throughput - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
            , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
            , series='alg'
            # , x_axis='TOTAL_THREADS'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        # add_plot_set(
        #     exp_dict
        #     # , name=plotDir + str(group) +'_Throughput-{INS_DEL_FRAC}-key={MAXKEY}-zipf={zipf}.png'
        #     # , title='Throughput - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY} - zipf:{zipf}'
        #     # , varying_cols_list=['zipf', 'MAXKEY', 'INS_DEL_FRAC']
        #     , name=plotDir + str(group) +'_joules-{INS_DEL_FRAC}-key={MAXKEY}.png'
        #     , title='joules - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='joules'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'Throughput-with-replay-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='Throughput with Replay - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='total_throughput_with_replay'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )



        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + str(group) + 'RQ-Throughput-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='RQ-Throughput - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='rq_throughput'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + str(group) + 'fallback_path_time-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='fallback_path_time - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='fallback_path_time'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'capacity-aborts-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='capacity-aborts - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='fast_htm_abort_capacity'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'stm-commits-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='stm-commits - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_stm_commit_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'htm-commits-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='htm-commits - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_htm_commit_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'stm-aborts-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='stm-aborts - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_stm_abort_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'htm-aborts-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='htm-aborts - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_htm_abort_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        #
        # No papi for HTM
        #
        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'PAPI_L2_TCM-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='PAPI_L2_TCM - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='PAPI_L2_TCM'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'PAPI_L3_TCM-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='PAPI_L3_TCM - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='PAPI_L3_TCM'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'PAPI_TOT_CYC-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='PAPI_TOT_CYC - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='PAPI_TOT_CYC'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDir + 'PAPI_TOT_INS-{INS_DEL_FRAC}-key={MAXKEY}-'+group+'.png'
        #     , title='PAPI_TOT_INS - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='PAPI_TOT_INS'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
