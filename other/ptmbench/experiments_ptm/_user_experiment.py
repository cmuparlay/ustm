#!/usr/bin/python3
from _basic_functions import *
import copy
from plot_style2 import *


color_list = [
      '#df7126' # 0
    , '#37946e' # 1
    , '#639bff' # 2
    , '#ac3232' # 3
    , '#76428a' # 4
    , '#fbf236' # 5
    , '#b0aca0' # 6
    , '#9e0b00' # 7
    , '#000000' # 8
    , '#fbf236' # 9
    , '#000000' # 10
    , '#ffffff' # 11
    , '#FFFFFF' #12
    , '#076794' #13
    , '#9db2bd' #14
]

unfiltered_algs = dict({
         ''                                       : dict(name=''                  , use=False , hatch=''   , color=''              , line_style=''             , line_width=''     , marker_style=''       , marker_size=''        )  
        , 'RedBlackTree_volatile_tl2.none.buffered_durable_tl2'                        : dict(name='RB-Volatile-TL2'    , use=False  , hatch=''     , color=color_list[14]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                          
        , 'RedBlackTree_tl2.none.buffered_durable_tl2'                                 : dict(name='Buffered-TL2'       , use=False  , hatch=''     , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                          
        , 'RedBlackTree_quadra_vr_fc.none'                                             : dict(name='Quadra'             , use=False  , hatch=''     , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                                  
        , 'RedBlackTree_trinity_vr_tl2.none'                                           : dict(name='Trinity'            , use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                        
        , 'RedBlackTree_trinity_vol.none'                                              : dict(name='RB-Volatile-Trinity', use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                        
        , 'RedBlackTree_redoopt.none'                                                  : dict(name='RedoOpt'            , use=False  , hatch=''     , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'RedBlackTree_tl2_copy_and_write_back_disabled.none.buffered_durable_tl2'    : dict(name='TL2-NoCopy&WB'      , use=False  , hatch=''     , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                        
        , 'RedBlackTree_trinity_vr_tl2.none'                                           : dict(name='Trinity'            , use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            


        , 'ext_bst_volatile_tl2.none.buffered_durable_tl2'                             : dict(name='ExtBst-Volatile-TL2' , use=False   , hatch=''     , color=color_list[14]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                          
        , 'ext_bst_tl2.none.buffered_durable_tl2'                                      : dict(name='Buffered-TL2'        , use=False   , hatch=''     , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                          
        , 'ext_bst_quadra_vr_fc.none'                                                  : dict(name='Quadra'              , use=False  , hatch=''     , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                                  
        , 'ext_bst_trinity_vr_tl2.none'                                                : dict(name='Trinity'             , use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                        
        , 'ext_bst_trinity_vol.none'                                                   : dict(name='ExtBst-Volatile-Trinity'  , use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                        
        , 'ext_bst_redoopt.none'                                                       : dict(name='RedoOpt'             , use=False  , hatch=''     , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'ext_bst_tl2_copy_and_write_back_disabled.none.buffered_durable_tl2'         : dict(name='TL2-NoCopy&WB'       , use=False   , hatch=''     , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'ext_bst_tl2_cpywb_chunks_disabled.none.buffered_durable_tl2'                : dict(name='TL2-NoChunksNoWB'    , use=False   , hatch=''     , color=color_list[6]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'ext_bst_trinity_vr_tl2_flush_is_noop.none'                                  : dict(name='Trinity-NoopFlush'   , use=False  , hatch=''     , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        


        , 'BTree_volatile_tl2.none.buffered_durable_tl2'                               : dict(name='BTree-Volatile-TL2' , use=True  , hatch=''     , color=color_list[14]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_tl2.none.buffered_durable_tl2'                                        : dict(name='Buffered-TL2'      , use=False  , hatch=''     , color=color_list[1]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_quadra_vr_fc.none'                                                    : dict(name='Quadra'            , use=False  , hatch=''     , color=color_list[2]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_trinity_vr_tl2.none'                                                  : dict(name='Trinity'           , use=False  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_trinity_vol.none'                                                     : dict(name='BTree-Volatile-Trinity'  , use=True  , hatch=''     , color=color_list[3]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_trinity_volatile_memory.none'                                         : dict(name='Trinity-Vol-Memory', use=False  , hatch=''     , color=color_list[5]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
        , 'BTree_redoopt.none'                                                         : dict(name='RedoOpt'           , use=False  , hatch=''     , color=color_list[4]   , line_style=(2,2)          , line_width='1'    , marker_style='o'      , marker_size='')                            
})


host = shell_to_str('hostname').rstrip('\r\n ')
supports_htm = 'rtm' in shell_to_str('lscpu')
supports_papi = (host != 'nasus')


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
    # set_dir_data     (exp_dict, os.getcwd() + '/data_red_black')
    cmd_compile = 'make bin_dir={__dir_run} -j'
    if not supports_papi: cmd_compile += ' has_libpapi=0'
    
    add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['0.5 0.5', '5.0 5.0', '50.0 50.0'])
    # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['50.0 50.0'])
    # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['0.5 0.5', '5.0 5.0'])
    # add_run_param    (exp_dict, 'INS_DEL_FRAC'      , ['0.5 0.5'])
    # add_run_param    (exp_dict, 'MAXKEY'            , [10000, 100000, 1000000])    
    # add_run_param    (exp_dict, 'MAXKEY'            , [100000])    
    add_run_param    (exp_dict, 'MAXKEY'            , [1000000])    
    add_run_param    (exp_dict, 'algorithm'         , list(filtered_algs.keys()))
    add_run_param    (exp_dict, 'thread_pinning'    , ['-pin ' + shell_to_str('cd ' + get_dir_tools(exp_dict) + ' ; ./get_pinning_cluster.sh', exit_on_error=True)])
    add_run_param    (exp_dict, 'millis'            , [20000])
    add_run_param    (exp_dict, 'TOTAL_THREADS'     , [1, 8, 16, 32, 64, 72, 96])  
    # add_run_param    (exp_dict, '__trials'          , [1, 2, 3])
    add_run_param    (exp_dict, '__trials'          , [1])

    #force prefill thread count to 1
    cmd_run = 'timeout 360 numactl --interleave=all time  ./{algorithm} -nwork {TOTAL_THREADS} -prefill-insert -nprefill 1 -insdel {INS_DEL_FRAC} -k {MAXKEY} -t {millis} {thread_pinning}'

    set_cmd_compile  (exp_dict, cmd_compile)
    set_cmd_run      (exp_dict, cmd_run) ## also adds data_fields to capture the outputs of the `time` command

    add_data_field   (exp_dict, 'alg'               , coltype='TEXT', extractor=extract_filtered_algs)
    add_data_field   (exp_dict, 'total_throughput'  , coltype='INTEGER', validator=is_positive)
    add_data_field   (exp_dict, 'PAPI_L3_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_L2_TCM'       , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_CYC'      , coltype='REAL')
    add_data_field   (exp_dict, 'PAPI_TOT_INS'      , coltype='REAL')
    add_data_field   (exp_dict, 'elapsed_millis'    , extractor=extract_elapsed_millis, validator=is_positive)
    add_data_field   (exp_dict, 'MILLIS_TO_RUN'     , coltype='TEXT', validator=is_positive)

    plotDirs = [
        '_plots/'
    ]    
    # order = filtered_algs.keys()
          
    alg_groups = dict()            
    # alg_groups['PTMs'] = ['Buffered-TL2', 'Quadra', 'Trinity', 'RedoOpt']
    # alg_groups['PTMs'] = ['Buffered-TL2', 'Trinity', 'Quadra', 'RedoOpt']
    # alg_groups['PTMs'] = ['Buffered-TL2', 'Trinity']
    # alg_groups['PTMs'] = ['Trinity', 'Trinity-NoopFlush']
    # alg_groups['PTMs'] = ['Buffered-TL2', 'TL2-NoCopy&WB']
    # alg_groups['PTMs'] = ['Buffered-TL2', 'TL2-NoCopy&WB', 'TL2-NoChunksNoWB']
    # alg_groups['PTMs'] = ['Buffered-TL2', 'Volatile-TL2', 'Trinity']
    # alg_groups['PTMs'] = ['Volatile-TL2', 'Trinity', 'Trinity-Vol-Memory']
    alg_groups['PTMs'] = ['BTree-Volatile-TL2', 'BTree-Volatile-Trinity']
        
    for i, group in enumerate(alg_groups):                        
        filter_sql = 'alg in ({})'.format(','.join(["'"+x+"'" for x in alg_groups[group]]))                        

        config2 = copy.deepcopy(base_config)
        config2['series_order'] = alg_groups[group]
        config2['width_inches'] = 18
        config2['height_inches'] = 12   
        config2['font_size'] = 48  
        config2['legend_columns'] = len(alg_groups[group])
        config2['logy'] = False
        

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
            , name=plotDirs[i] + 'Throughput-{INS_DEL_FRAC}-key={MAXKEY}.png'
            , title='Throughput - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
            , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='total_throughput'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'PAPI_L2_TCM-{INS_DEL_FRAC}-key={MAXKEY}.png'
            , title='PAPI_L2_TCM - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
            , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='PAPI_L2_TCM'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )

        add_plot_set(
            exp_dict
            , name=plotDirs[i] + 'PAPI_L3_TCM-{INS_DEL_FRAC}-key={MAXKEY}.png'
            , title='PAPI_L3_TCM - Ins/Del:{INS_DEL_FRAC} - MaxK:{MAXKEY}'
            , varying_cols_list=['MAXKEY', 'INS_DEL_FRAC']
            , series='alg'
            , x_axis='TOTAL_THREADS'
            , y_axis='PAPI_L3_TCM'
            , plot_type=plot_bars
            , plot_cmd_args=config2
            , filter=filter_sql
        )
