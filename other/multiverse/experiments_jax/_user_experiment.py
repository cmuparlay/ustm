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

host = shell_to_str('hostname').rstrip('\r\n ')


unfiltered_algs = dict({
         ''                             : dict(name=''            , use=False , hatch=''   , color=''              , line_style=''             , line_width=''     , marker_style=''       , marker_size='')
        , 'brown_abtree_tm_auto.norec_' + str(host) : dict(name='Norec' , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_abtree_tm_auto.tl2_' + str(host)   : dict(name='TL2' , use=True  , hatch=''    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'abtree.dcotl_' + str(host)           : dict(name='DCTL' , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'abtree.multiverse_' + str(host)      : dict(name='Multiverse' , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'abtree.tinystm_' + str(host)      : dict(name='TinySTM' , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')

        # , 'abtree.multiverse_mode1_only'      : dict(name='Multiverse-M1' , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        # , 'abtree.multiverse_mode2_only'      : dict(name='Multiverse-M2' , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')

        , 'brown_sigouin_int_avl_tm_auto.norec_' + str(host) : dict(name='Norec' , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_sigouin_int_avl_tm_auto.tl2_' + str(host)   : dict(name='TL2' , use=True  , hatch=''    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_sigouin_int_avl.dcotl_' + str(host)         : dict(name='DCTL' , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_sigouin_int_avl.multiverse_' + str(host)    : dict(name='Multiverse' , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_sigouin_int_avl.tinystm_' + str(host)      : dict(name='TinySTM' , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')

        , 'brown_ext_bst_tm_auto.norec_' + str(host) : dict(name='Norec' , use=True  , hatch=''    , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_ext_bst_tm_auto.tl2_' + str(host)   : dict(name='TL2' , use=True  , hatch=''    , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_ext_bst.dcotl_' + str(host)         : dict(name='DCTL' , use=True  , hatch=''    , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_ext_bst.multiverse_' + str(host)    : dict(name='Multiverse' , use=True  , hatch=''    , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'brown_ext_bst.tinystm_' + str(host)      : dict(name='TinySTM' , use=True  , hatch=''    , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
})



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

USE_INERVALS = False
SINGLE_SOCKET = False
ZIPF_DIST = False

def define_experiment(exp_dict, args):
    set_dir_compile  (exp_dict, os.getcwd() + '/../')
    set_dir_tools    (exp_dict, os.getcwd() + '/../setbench/tools')
    set_dir_run      (exp_dict, os.getcwd() + '/../bin')
    set_dir_data     (exp_dict, os.getcwd() + '/data')
    cmd_compile = 'make bin_dir={__dir_run} -j all'


    add_run_param    (exp_dict, 'algorithm'         , list(filtered_algs.keys()))
    
    # add_run_param    (exp_dict, 'MAXKEY'            , [1000000, 2000000])
    # add_run_param    (exp_dict, 'MAXKEY'            , [2000000])
    add_run_param    (exp_dict, 'MAXKEY'            , [2000000])

    add_run_param    (exp_dict, 'millis'            , [20000])
    add_run_param    (exp_dict, 'RQ_THREADS'     , [0])

    if ZIPF_DIST:
        add_run_param    (exp_dict, 'ZIPF'     , ["-dist-zipf 0.9"])
    else:
        add_run_param    (exp_dict, 'ZIPF'     , [""])

    if SINGLE_SOCKET:
        # jax only numa node0
        add_run_param    (exp_dict, 'NUMCTL'     , ["--i 0"])
        add_run_param    (exp_dict, 'thread_pinning'    , ['-pin 0-23,96-119'])
    else:
        add_run_param    (exp_dict, 'NUMCTL'     , ["--interleave=all"])
        # jax
        add_run_param    (exp_dict, 'thread_pinning'    , ['-pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191'])




    if USE_INERVALS:
        # add_run_param    (exp_dict, 'INTERVALS'      , ['4 8 5.0 5.0 0.0 10000 8 5.0 5.0 0.01 10000 8 5.0 5.0 0 10000 8 5.0 5.0 0.01 10000'])
        # add_run_param    (exp_dict, 'INTERVALS'      , ['4 0 5.0 5.0 0.0 10000 16 5.0 5.0 0.01 10000 0 5.0 5.0 0 10000 16 5.0 5.0 0.01 10000'])

        # 100k rq size varying rq %
        add_run_param    (exp_dict, 'INTERVALS'      , ['4 16 5.0 5.0 0.00 100000 16 5.0 5.0 0.1 100000 16 5.0 5.0 0.00 100000 16 5.0 5.0 0.1 100000'])

        # add_run_param    (exp_dict, 'INTERVALS'      , ['4 8 5.0 5.0 0.01 10000 8 5.0 5.0 0.01 100000 8 5.0 5.0 0.01 10000 8 5.0 5.0 0.01 100000'])
        # add_run_param    (exp_dict, 'INTERVALS'      , ['4 16 5.0 5.0 0.01 10000 16 5.0 5.0 0.01 100000 16 5.0 5.0 0.01 10000 16 5.0 5.0 0.01 100000'])

        add_run_param    (exp_dict, 'WORK_THREADS'     , [64])

        # add_run_param    (exp_dict, 'prefillsize'     , [1000000])

        cmd_run = 'LD_PRELOAD=../setbench/lib/libmimalloc.so timeout 360 numactl {NUMCTL} time ./{algorithm} -nprefill {WORK_THREADS} -nwork {WORK_THREADS} -nrq {RQ_THREADS} -interval {INTERVALS} -k {MAXKEY} -t {millis} {ZIPF} {thread_pinning}'
    else:
        # # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['0.0 0.0', '5.0 5.0', '10.0 10.0', '25.0 25.0', '47.5 47.5'])
        # # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['5.0 5.0', '20.0 20.0', '47.5 47.5'])
        # # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['5.0 5.0', '20.0 20.0'])
        add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['5.0 5.0'])

        add_run_param    (exp_dict, 'RQ'      , ['0', '0.1', '0.01'])

        add_run_param    (exp_dict, 'UPDATE_ONLY_THREADS'     , [0, 16])

        add_run_param    (exp_dict, 'RQ_SIZE'            , [10000])

        # add_run_param    (exp_dict, 'WORK_THREADS'     , [8, 24, 40, 64, 72, 96, 112, 144, 192, 240])
        # add_run_param    (exp_dict, 'WORK_THREADS'     , [64, 96, 112, 144, 192, 224])
        add_run_param    (exp_dict, 'WORK_THREADS'     , [56, 76, 104, 124, 152, 172])
        # add_run_param    (exp_dict, 'WORK_THREADS'     , [8, 16, 32])
        # add_run_param    (exp_dict, 'WORK_THREADS'     , [8, 40, 72, 104])

        cmd_run = 'LD_PRELOAD=../setbench/lib/libmimalloc.so timeout 360 numactl {NUMCTL} time ./{algorithm} -nprefill {WORK_THREADS} -nwork {WORK_THREADS} -nupdater {UPDATE_ONLY_THREADS} -nrq {RQ_THREADS} -rqsize {RQ_SIZE} -rq {RQ} -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {ZIPF} {thread_pinning}'


    add_run_param    (exp_dict, '__trials'          , list(range(0, 5)))

    set_cmd_compile  (exp_dict, cmd_compile)
    set_cmd_run      (exp_dict, cmd_run) ## also adds data_fields to capture the outputs of the `time` command

    add_data_field   (exp_dict, 'alg'               , coltype='TEXT', extractor=extract_filtered_algs)
    add_data_field   (exp_dict, 'TOTAL_THREADS'  , coltype='INTEGER')
    add_data_field   (exp_dict, 'PAPI_L3_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_L2_DCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_CYC'      , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_INS'      , coltype='REAL')
    add_data_field   (exp_dict, 'elapsed_millis'    , extractor=extract_elapsed_millis, validator=is_positive)
    add_data_field   (exp_dict, 'MILLIS_TO_RUN'     , coltype='TEXT', validator=is_positive)

    add_data_field   (exp_dict, 'total_ops', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_find', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_rq', coltype='INTEGER')

    add_data_field   (exp_dict, 'total_inserts', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_deletes', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_updates', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_queries', coltype='INTEGER')

    add_data_field   (exp_dict, 'find_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'rq_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'update_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'query_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_throughput', coltype='INTEGER')

    add_data_field   (exp_dict, 'sum_commits_snapshot_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_commits_mode2_snapshot_total', coltype='INTEGER', extractor=extract_possibly_empty_int)

    add_data_field   (exp_dict, 'num_occupied_locks', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'percentage_of_occupied_locks', coltype='INTEGER', extractor=extract_possibly_empty_float)

    add_data_field   (exp_dict, 'num_mode_changes', coltype='INTEGER', extractor=extract_possibly_empty_float)

    plotDirs = [
        '_plots/'
    ]

    alg_groups = dict()

    # alg_groups['all'] = list(filtered_algs.values())
    alg_groups['custom-order'] = ['Norec', 'TinySTM', 'TL2', 'DCTL', 'Multiverse']
    # alg_groups['custom-order'] = ['Norec', 'TinySTM', 'TL2', 'DCTL', 'Multiverse', 'Multiverse-M1', 'Multiverse-M2']

    # full_algorithms = ['brown_abtree_tm_auto.norec_' + str(host),'brown_abtree_tm_auto.tl2_' + str(host),'abtree.dcotl_' + str(host),'abtree.multiverse_' + str(host),'abtree.tinystm_' + str(host)]
    # full_algorithms = ['brown_sigouin_int_avl_tm_auto.norec_' + str(host),'brown_sigouin_int_avl_tm_auto.tl2_' + str(host),'brown_sigouin_int_avl.dcotl_' + str(host),'brown_sigouin_int_avl.multiverse_' + str(host),'brown_sigouin_int_avl.tinystm_' + str(host)]
    full_algorithms = ['brown_ext_bst_tm_auto.norec_' + str(host),'brown_ext_bst_tm_auto.tl2_' + str(host),'brown_ext_bst.dcotl_' + str(host),'brown_ext_bst.multiverse_' + str(host),'brown_ext_bst.tinystm_' + str(host)]

    for i, group in enumerate(alg_groups):
        filter_sql = 'algorithm in ({})'.format(','.join(["'"+x+"'" for x in full_algorithms]))
        filter_sql = filter_sql + 'and alg in ({})'.format(','.join(["'"+x+"'" for x in alg_groups[group]]))

        config2 = copy.deepcopy(base_config)
        config2['series_order'] = alg_groups[group]
        config2['width_inches'] = 18
        config2['height_inches'] = 12
        config2['font_size'] = 48
        config2['legend_columns'] = len(alg_groups[group])
        config2['logy'] = False
        config2['error_bar_width'] = 10


        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'legend.png'
            , series='alg'
            # , x_axis='TOTAL_THREADS'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars_legend
            #, plot_cmd_args='--legend-only --legend-columns 3'
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        # config3 = copy.deepcopy(config2)
        # config3['series_order'] = full_algorithms
        # config3['legend_columns'] = len(full_algorithms)

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'legend2.png'
        #     , series='algorithm'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='total_throughput'
        #     , plot_type=plot_bars_legend
        #     #, plot_cmd_args='--legend-only --legend-columns 3'
        #     , plot_cmd_args=config3
        #     , filter=filter_sql
        # )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'Throughput-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
            , title='Throughput - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
            , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
            , series='alg'
            # , x_axis='TOTAL_THREADS'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'RQ_throughput-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
            , title='RQ_throughput - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
            , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
            , series='alg'
            # , x_axis='TOTAL_THREADS'
            , x_axis='TOTAL_THREADS'
            , y_axis='rq_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
        


        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'Query_Throughput-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='Query_Throughput - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='query_throughput'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'Update_throughput-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='Update_throughput - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='update_throughput'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'snapshot-commits-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='snapshot-commits - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_commits_snapshot_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'mode2-snapshot-commits-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='mode2-snapshot-commits - INS_DEL_FAC={INS_DEL_FRAC}-RQ={RQ}-RQ_THREADS={RQ_THREADS}-UPDTATE_ONLY_THREADS={UPDATE_ONLY_THREADS}-MAX_KEY={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     # , x_axis='TOTAL_THREADS'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_commits_mode2_snapshot_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'PAPI_TOT_INS-{INS_DEL_FRAC}-RQ_Threads-{RQ_THREADS}-key={MAXKEY}.png'
        #     , title='PAPI_TOT_INS - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='PAPI_TOT_INS'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'PAPI_TOT_CYC-{INS_DEL_FRAC}-RQ_Threads-{RQ_THREADS}-key={MAXKEY}.png'
        #     , title='PAPI_TOT_CYC - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='PAPI_TOT_CYC'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'PAPI_L2_TCM-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='PAPI_L2_TCM-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='PAPI_L2_DCM'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'PAPI_L3_TCM-{INS_DEL_FRAC}-RQ_Threads-{RQ_THREADS}-key={MAXKEY}.png'
        #     , title='PAPI_L3_TCM - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='PAPI_L3_TCM'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'num_occupied_locks-{INS_DEL_FRAC}-RQ_Threads-{RQ_THREADS}-key={MAXKEY}.png'
        #     , title='num_occupied_locks - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='num_occupied_locks'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'percentage_of_occupied_locks-{INS_DEL_FRAC}-RQ_Threads-{RQ_THREADS}-key={MAXKEY}.png'
        #     , title='percentage_of_occupied_locks - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
        #     , varying_cols_list=['MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='percentage_of_occupied_locks'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'mode-changes-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}.png'
        #     , title='num_mode_changes-IN_DEL={INS_DEL_FRAC}-RQ={RQ}-RQ_SIZE={RQ_SIZE}-RQ_T={RQ_THREADS}-UPDT_T={UPDATE_ONLY_THREADS}-mkey={MAXKEY}'
        #     , varying_cols_list=['RQ_SIZE', 'MAXKEY', 'RQ_THREADS', 'INS_DEL_FRAC', 'RQ', 'UPDATE_ONLY_THREADS']
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='num_mode_changes'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )


