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
         ''                         : dict(name=''            , use=False , hatch=''   , color=''              , line_style=''             , line_width=''     , marker_style=''       , marker_size='')        
        , 'db_dctl'      : dict(name='DB-DCOTL' , use=True  , hatch='' , color=color_list[0]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
        , 'db_multiverse'      : dict(name='DB-MULTIVERSE'  , use=True  , hatch='' , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')
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
    cmd_compile = 'make bin_dir={__dir_run} -j all'

    add_run_param    (exp_dict, 'algorithm'         , list(filtered_algs.keys()))
        
    add_run_param    (exp_dict, 'thread_pinning'    , ['-pin 0-23,96-119,24-47,120-143,48-71,144-167,72-95,168-191'])

    add_run_param    (exp_dict, 'millis'            , [20000])

    add_run_param    (exp_dict, 'RQSIZE'            , [8])
    add_run_param    (exp_dict, 'ZIPF'            , [0.9])

    add_run_param    (exp_dict, 'PREFILL_THREADS'               , [128])              
    # add_run_param    (exp_dict, 'WORK_THREADS'                  , [0, 1, 2, 3, 4, 5, 6, 8, 12, 16, 24, 32, 64, 126])
    # add_run_param    (exp_dict, 'WORK_THREADS'                  , [0, 1, 2, 3, 4, 8, 16, 32])
    add_run_param    (exp_dict, 'WORK_THREADS'                  , [1])
    add_run_param    (exp_dict, 'COMPANY_ANALYTICS_THREADS'     , [0])
    
    add_run_param    (exp_dict, 'COMPANY_THREAD_SLEEP_NANOSECONDS'      , ["1000"])
    add_run_param    (exp_dict, 'COMPANY_THREAD_ZIPF'                   , ["1.5 "])

    add_run_param    (exp_dict, 'PREFILL_ITEM_COUNT'                , ["10000000"])
    add_run_param    (exp_dict, 'PREFILL_ORDER_COUNT'               , ["10000000"])
    add_run_param    (exp_dict, 'CATEGORY_COUNT'                    , ["10000"])
    
    add_run_param    (exp_dict, 'MAX_CUSTOMER_ID'                   , ["100000000"])
    add_run_param    (exp_dict, 'MAX_ITEM_ID'                       , ["1000000000"])
    add_run_param    (exp_dict, 'MAX_ORDER_ID'                      , ["1000000000"])
    add_run_param    (exp_dict, 'MAX_DATE'                          , ["10000"])
    add_run_param    (exp_dict, 'MAX_ITEMS_PER_ORDER'               , ["15"])
    
    add_run_param    (exp_dict, 'TXN_FRAC_ADD_NEW_ORDER'                    , ["100"])
    add_run_param    (exp_dict, 'TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY'    , ["0"])
    add_run_param    (exp_dict, 'TXN_FRAC_ADD_ITEM'                         , ["0"])
    add_run_param    (exp_dict, 'TXN_FRAC_UPDATE_ITEM'                      , ["0"])

    add_run_param    (exp_dict, 'numactlMode'                      , ["numactl --interleave=all"])

    add_run_param    (exp_dict, '__trials'          , list(range(1, 10)))
        
    cmd_run = 'LD_PRELOAD=../../setbench/lib/libmimalloc.so timeout 360 {numactlMode} ./{algorithm} -t {millis} -rqsize {RQSIZE} -zipf {ZIPF} -nprefill {PREFILL_THREADS} -n {WORK_THREADS} -nca {COMPANY_ANALYTICS_THREADS} -cats {COMPANY_THREAD_SLEEP_NANOSECONDS} -zipf {COMPANY_THREAD_ZIPF} -kcategories {CATEGORY_COUNT} -kcustomers {MAX_CUSTOMER_ID} -kitems {MAX_ITEM_ID} -korders {MAX_ORDER_ID} -kdates {MAX_DATE} -max-items-per-order {MAX_ITEMS_PER_ORDER} -prefillitems {PREFILL_ITEM_COUNT} -prefillorders {PREFILL_ORDER_COUNT}  -txn-add-order {TXN_FRAC_ADD_NEW_ORDER} -txn-customer-history-lookup {TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY} -txn-add-item {TXN_FRAC_ADD_ITEM} -txn-update-item {TXN_FRAC_UPDATE_ITEM} {thread_pinning}'

    set_cmd_compile  (exp_dict, cmd_compile)
    set_cmd_run      (exp_dict, cmd_run) ## also adds data_fields to capture the outputs of the `time` command

    add_data_field   (exp_dict, 'alg'               , coltype='TEXT', extractor=extract_filtered_algs)
    add_data_field   (exp_dict, 'total_throughput'  , coltype='INTEGER', validator=is_positive)
    add_data_field   (exp_dict, 'TOTAL_THREADS'  , coltype='INTEGER')
    add_data_field   (exp_dict, 'PAPI_L3_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_L2_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_CYC'      , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_INS'      , coltype='REAL')
    add_data_field   (exp_dict, 'elapsed_millis'    , extractor=extract_elapsed_millis, validator=is_positive)
    add_data_field   (exp_dict, 'MILLIS_TO_RUN'     , coltype='TEXT', validator=is_positive)

    add_data_field   (exp_dict, 'sum_aborts_snapshot_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_commits_snapshot_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_aborts_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_commits_total', coltype='INTEGER', extractor=extract_possibly_empty_int)

    add_data_field   (exp_dict, 'sum_aborts_snapshot_tryVersion_read_inconsistent_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_aborts_writer_failed_rdset_validation_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_aborts_snapshot_vlist_failed_find_ts_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_aborts_tl2_load_read_inconsistent_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'sum_aborts_tl2_store_failed_lock_acquire_total', coltype='INTEGER', extractor=extract_possibly_empty_int)
    add_data_field   (exp_dict, 'abosum_rts_tl2_store_write_inconsistent_total', coltype='INTEGER', extractor=extract_possibly_empty_int)

    add_data_field   (exp_dict, 'num_unversions', coltype='INTEGER', extractor=extract_possibly_empty_int)
    
    
    add_data_field   (exp_dict, 'total_new_order_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_new_item_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_update_item_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_random_items_lookup_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_customer_order_history_lookup_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_company_analytics_txn', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_updates', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_worker_reads', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_analytic_reads', coltype='INTEGER')
    add_data_field   (exp_dict, 'worker_thread_update_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'worker_thread_query_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'analytic_thread_query_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'worker_thread_total_throughput', coltype='INTEGER')
    add_data_field   (exp_dict, 'total_throughput', coltype='INTEGER')

    plotDirs = [
        '_plots/'
    ]    
                    
    alg_groups = dict()     

    alg_groups['all'] = list(filtered_algs.values())
 
    for i, group in enumerate(alg_groups):                        
        filter_sql = 'alg in ({})'.format(','.join(["'"+x+"'" for x in alg_groups[group]]))                        

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
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars_legend
            #, plot_cmd_args='--legend-only --legend-columns 3'
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'Throughput.png'
            , title='Throughput'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
        
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'worker_thread_update_throughput.png'
            , title='worker_thread_update_throughput'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='worker_thread_update_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
    
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'worker_thread_query_throughput.png'
            , title='worker_thread_query_throughput'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='worker_thread_query_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
    
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'analytic_thread_query_throughput.png'
            , title='analytic_thread_query_throughput'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='analytic_thread_query_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
    
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'worker_thread_total_throughput.png'
            , title='worker_thread_total_throughput'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='worker_thread_total_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
        
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'total_random_items_lookup_txn.png'
            , title='total_random_items_lookup_txn'
            , varying_cols_list=[]
            , series='alg'            
            , x_axis='TOTAL_THREADS'
            , y_axis='total_random_items_lookup_txn'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'snapshot_commits.png'
            , title='snapshot_commits'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='sum_commits_snapshot_total'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'snapshot_commits-per-trial-{TOTAL_THREADS}-threads.png'
        #     , title='snapshot_commits-{TOTAL_THREADS}'
        #     , varying_cols_list=['TOTAL_THREADS']
        #     , series='alg'
        #     , x_axis='__trials'
        #     , y_axis='sum_commits_snapshot_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )


        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_snapshot_tryVersion_read_inconsistent.png'
        #     , title='aborts_snapshot_tryVersion_read_inconsistent'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_snapshot_tryVersion_read_inconsistent_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_writer_failed_rdset_validation.png'
        #     , title='aborts_writer_failed_rdset_validation'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_writer_failed_rdset_validation_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_snapshot_vlist_failed_find_ts.png'
        #     , title='aborts_snapshot_vlist_failed_find_ts'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_snapshot_vlist_failed_find_ts_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_tl2_load_read_inconsistent.png'
        #     , title='aborts_tl2_load_read_inconsistent'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_tl2_load_read_inconsistent_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_tl2_store_failed_lock_acquire.png'
        #     , title='aborts_tl2_store_failed_lock_acquire'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_tl2_store_failed_lock_acquire_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'aborts_tl2_store_write_inconsistent.png'
        #     , title='aborts_tl2_store_write_inconsistent'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     , y_axis='sum_aborts_tl2_store_write_inconsistent_total'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )


        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'snapshot_aborts.png'
            , title='snapshot_aborts'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='sum_aborts_snapshot_total'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'commits_total.png'
            , title='commits_total'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='sum_commits_total'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'aborts_total.png'
            , title='aborts_total'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='sum_aborts_total'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'total_company_analytics_txn.png'
            , title='total_company_analytics_txn'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_company_analytics_txn'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )        

        # add_plot_set(
        #     exp_dict
        #     , name=plotDirs[i] + 'PAPI_TOT_INS.png'
        #     , title='PAPI_TOT_INS'
        #     , varying_cols_list=[]
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
        #     , name=plotDirs[i] + 'PAPI_TOT_CYC.png'
        #     , title='PAPI_TOT_CYC'
        #     , varying_cols_list=[]
        #     , series='alg'
        #     , x_axis='TOTAL_THREADS'
        #     # , x_axis='__trials'
        #     , y_axis='PAPI_TOT_CYC'
        #     , plot_type=plot_bars
        #     , plot_cmd_args=config2
        #     , filter=filter_sql
        # )
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'PAPI_L2_TCM.png'
            , title='PAPI_L2_TCM'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            # , x_axis='__trials'
            , y_axis='PAPI_L2_TCM'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'PAPI_L3_TCM.png'
            , title='PAPI_L2_TCM'
            , varying_cols_list=[]
            , series='alg'
            , x_axis='TOTAL_THREADS'
            # , x_axis='__trials'
            , y_axis='PAPI_L3_TCM'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )


        # config3 = copy.deepcopy(base_config)
        # config3['series_order'] = alg_groups[group]
        # config3['width_inches'] = 22
        # # config3['width_inches'] = 11
        # config3['height_inches'] = 8   
        # config3['font_size'] = 48  
        # config3['legend_columns'] = len(alg_groups[group])
        # config3['logy'] = False
        # config3['error_bar_width'] = 0
        # filter_sql2 = 'TOTAL_THREADS = 128 and alg in ({})'.format(','.join(["'"+x+"'" for x in alg_groups[group]]))