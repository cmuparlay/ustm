######################################################################
## custom plot styling code
######################################################################

import pandas as pd
import matplotlib as mpl
mpl.use('Agg')
import matplotlib.pyplot as plt
import seaborn as sns
from run_experiment import get_seaborn_series_styles, tee_fatal
import copy

def set_config_defaults(config):
    if 'error_bar_width' not in config:     config['error_bar_width']   = 4
    if 'title' not in config:               config['title']             = ''
    if 'legend_title' not in config:        config['legend_title']      = None
    if 'logy' not in config:                config['logy']              = False
    if 'stacked' not in config:             config['stacked']           = False
    if 'legend_columns' not in config:      config['legend_columns']    = 1
    if 'width_inches' not in config:        config['width_inches']      = 8
    if 'height_inches' not in config:       config['height_inches']     = 5
    # if 'dots_per_inch' not in config:       config['dots_per_inch']     = 100

def set_plt_and_rc_params(config):
    if 'font_size' in config:
        plt.rcParams.update({'font.size': config['font_size']})
    if 'dark_mode' in config and config['dark_mode']:
        plt.style.use('dark_background')

def create_doubled_dict_from(singled_dict):
    ## return a dict containing (k,v) AND (_k,v), for each k,v in singled_dict
    doubled_dict = dict()
    for k in singled_dict.keys():
        doubled_dict[k] = singled_dict[k]
        doubled_dict['_' + str(k)] = singled_dict[k]
    return doubled_dict

def get_plot_line_style_kwargs(config, dict_key_field):
    ## get these kwargs as default
    plot_kwargs = get_seaborn_series_styles(config['series_order'], plot_func=sns.lineplot)

    ## replace them as appropriate
    plot_kwargs['style'] = dict_key_field

    if 'marker_style_map' in config:
        plot_kwargs['markers'] = create_doubled_dict_from(config['marker_style_map'])

    if 'line_style_map' in config:
        plot_kwargs['dashes'] = create_doubled_dict_from(config['line_style_map'])

    if 'color_map' in config:
        plot_kwargs['palette'] = create_doubled_dict_from(config['color_map'])

    plot_kwargs['markersize'] = 12
    if 'markersize' in config:
        plot_kwargs['markersize'] = config['markersize']

    return plot_kwargs

def plot_line_regions(filename, column_filters, data, series_name, x_name, y_name, do_save=True, legend=False, do_close=True, config=dict()):
    plt.ioff() # stop plots from being shown in jupyter
    # print('series_name={}'.format(series_name))

    if 'series_order' not in config:
        tee_fatal('if using plot_line_regions then must provide "series_order" list to config dict')

    set_config_defaults(config)
    set_plt_and_rc_params(config)
    plot_kwargs = get_plot_line_style_kwargs(config, series_name)
    if not legend: plot_kwargs['legend'] = False

    fig, ax = plt.subplots()
    sns.lineplot(ax=ax, data=data, x=x_name, y=y_name, hue=series_name, ci=100, **plot_kwargs)
    plt.tight_layout()

    if do_save: mpl.pyplot.savefig(filename)
    # print(data) ; print('## SAVED FIGURE {}'.format(filename))
    if do_close: plt.close()

def plot_line_regions_legend(filename, column_filters, data, series_name, x_name, y_name, config=dict()):
    plot_line_regions(filename, column_filters, data, series_name, x_name, y_name, do_save=False, legend=True, do_close=False, config=config)

    legend_markerscale = 1
    if 'legend_markerscale' in config: legend_markerscale = config['legend_markerscale']

    axi = plt.gca()
    handles, labels = axi.get_legend_handles_labels()

    fig_legend, axi = plt.subplots()
    fig_legend.legend(handles[1:], labels[1:], loc='center', frameon=False, ncol=4, markerscale=legend_markerscale)

    axi.xaxis.set_visible(False)
    axi.yaxis.set_visible(False)
    axi.axes.set_visible(False)

    plt.tight_layout()
    fig_legend.savefig(filename, bbox_inches='tight')
    plt.close()

def plot_bars(filename, column_filters, data, series_name, x_name, y_name, do_save=True, legend=False, do_close=True, config=dict()):
    plt.ioff() # stop plots from being shown in jupyter
    # print('series_name={}'.format(series_name))

    if 'series_order' not in config:
        tee_fatal('if using plot_bars then must provide "series_order" list to config dict')

    set_config_defaults(config)
    set_plt_and_rc_params(config)

    plot_kwargs = dict(
        legend=False
        , title=config['title']
        , kind='bar'
        , figsize=(config['width_inches'], config['height_inches'])
        , width=0.75
        , linewidth=2.0
        , zorder=10
        , logy=config['logy']        
        # , edgecolor='black'
    )
    if config['stacked']: plot_kwargs['stacked'] = True

    if 'dark_mode' in config and config['dark_mode']:
        plot_kwargs['edgecolor'] = 'white'
    else:
        plot_kwargs['edgecolor'] = 'black'

    tmean   = pd.pivot_table(data, columns=series_name, index=x_name, values=y_name, aggfunc='mean')
    tmin    = pd.pivot_table(data, columns=series_name, index=x_name, values=y_name, aggfunc='min')
    tmax    = pd.pivot_table(data, columns=series_name, index=x_name, values=y_name, aggfunc='max')
    # print(tmean.head())

    ## sort dataframes by algorithms in this order:
    if 'series_order' in config:
        tmean   = tmean.reindex(config['series_order'], axis=1)
        tmin    = tmin.reindex(config['series_order'], axis=1)
        tmax    = tmax.reindex(config['series_order'], axis=1)
    # print(tmean.head())

    ## compute error bars
    tpos_err = tmax - tmean
    tneg_err = tmean - tmin
    err = [[tpos_err[c], tneg_err[c]] for c in tmean]
    # print("error bars {}".format(err))

    if config['error_bar_width'] > 0:
        for e in err[0]:
            if len([x for x in e.index]) <= 1:
                print("note : forcing NO error bars because index is too small: {}".format(e.index))
                config['error_bar_width'] = 0

    legend_kwargs = dict(
        title=config['legend_title']
        , ncol=config['legend_columns']
        # , loc='upper center'
        # , bbox_to_anchor=(0.5, -0.1)
        # , fancybox=True
        # , shadow=True
    )

    fig, ax = plt.subplots()

    if config['error_bar_width'] == 0:
        chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)
    else:
        plot_kwargs['yerr'] = err
        plot_kwargs['error_kw'] = dict(elinewidth=config['error_bar_width'], ecolor='red')
        chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)

    chart.grid(axis='y', zorder=0)

    ax = plt.gca()

    chart.set_xticklabels(chart.get_xticklabels(), ha="center", rotation=0)

    bars = ax.patches
    if 'hatch_map' in config:
        assert('series_order' in config)
        # print('config["series_order"]={}'.format(config["series_order"]))
        # print('len(config["series_order"])={}'.format(len(config["series_order"])))
        # print('tmean={}'.format(tmean))
        # print('len(tmean)={}'.format(len(tmean)))
        # print('len(tmean.columns.values)={}'.format(len(tmean.columns.values)))
        assert(len(tmean.columns.values) == len(config['series_order']))
        k = 0
        for c in range(len(config['series_order'])):
            for i in range(len(tmean)):
                curr_series = config['series_order'][c]
                bars[k].set_hatch(config['hatch_map'][curr_series])
                if 'color_map' in config and config['color_map'][curr_series]:
                    # print("COLOR MAP!!! assigning {} to bar {}".format(config['color_map'][curr_series], k))
                    bars[k].set_fc(config['color_map'][curr_series])
                k += 1
    else:
        patterns = ( 'x', '/', '//', 'O', 'o', '\\', '\\\\', '-', '+', ' ' )
        hatches = [p for p in patterns for i in range(len(tmean))]
        for bar, hatch in zip(bars, hatches):
            bar.set_hatch(hatch)

    if 'xtitle' in config and not config['xtitle']:
        # ax.xaxis.set_label_text('foo')
        ax.xaxis.label.set_visible(False)

    plt.tight_layout()
    if do_save: mpl.pyplot.savefig(filename)
    if do_close: plt.close()

def plot_bars_legend(filename, column_filters, data, series_name, x_name, y_name, config=dict()):
    if 'legend_columns' not in config: config['legend_columns'] = 3
    if 'frameon' not in config: config['frameon'] = False

    plot_bars(filename, column_filters, data, series_name, x_name, y_name, do_save=False, legend=True, do_close=False, config=config)
    axi = plt.gca()
    handles, labels = axi.get_legend_handles_labels()

    fig_legend, axi = plt.subplots()
    fig_legend.legend(handles[:], labels[:], loc='center', frameon=False, ncol=config['legend_columns'])

    axi.xaxis.set_visible(False)
    axi.yaxis.set_visible(False)
    axi.axes.set_visible(False)

    plt.tight_layout()
    fig_legend.savefig(filename, bbox_inches='tight')
    plt.close()
