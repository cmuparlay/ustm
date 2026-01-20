#!/usr/bin/python3

import numpy as np
import matplotlib as mpl
#mpl.use('Agg')
import matplotlib.pyplot as plt
import pandas as pd
import matplotlib.ticker as ticker
import seaborn as sns
from matplotlib.ticker import FuncFormatter
from matplotlib import rcParams
from matplotlib.colors import ListedColormap

def plot_df(kind, data, y, x='', series='', fig=None, ax=None, \
        title='', xlabel='', ylabel='', logx=False, logy=False, legend=True, \
        plot_call_kwargs=dict(), legend_call_kwargs=dict(), **kwargs):

    ## check for conflicing arguments
    if not fig: assert(not ax)
    if not ax: assert(not fig)
    if not x: assert(series) ## one of x or series must be defined
    if not series: assert(x) ## one of x or series must be defined

    #########################################
    ## setup plot command args/config
    #########################################

    plot_kwargs = dict(kind=kind)
    plot_kwargs.update(plot_call_kwargs)
    if legend: plot_kwargs['legend'] = legend
    if title: plot_kwargs['title'] = title
    if logx: plot_kwargs['logx'] = logx
    if logy: plot_kwargs['logy'] = logy

    legend_kwargs = dict({
          'title': None
        , 'loc': 'upper center'
        , 'bbox_to_anchor': (0.5, -0.1)
        , 'fancybox': True
        , 'shadow': True
        , 'ncol': 3
    })
    legend_kwargs.update(legend_call_kwargs)

    if not ax or not fig: fig, ax = plt.subplots()

    #########################################
    ## pivot/aggregate data
    #########################################

    pivot_table_kwargs = dict()
    if series: pivot_table_kwargs['columns'] = series
    if x: pivot_table_kwargs['index'] = x
    if y: pivot_table_kwargs['values'] = y

    tmean = pd.pivot_table(data, **pivot_table_kwargs, aggfunc='mean')
    tmin = pd.pivot_table(data, **pivot_table_kwargs, aggfunc='min')
    tmax = pd.pivot_table(data, **pivot_table_kwargs, aggfunc='max')

    # display(tmean.head())

    ## compute error bars

    tpos_err = tmax - tmean
    tneg_err = tmean - tmin
    err = [[tpos_err[c], tneg_err[c]] for c in tmean]
    # print("error bars={}".format(err))

    no_error_bars = False
    for e in err[0]:
        if len([x for x in e.index]) <= 1:
            print("note : forcing NO error bars because index is too small: {}".format(e.index))
            no_error_bars = True
            break

    #########################################
    ## do the actual plotting
    #########################################

    if no_error_bars:
        chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)
    else:
        if kind == 'bar':
            plot_kwargs['yerr'] = err
            chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs)
        else:
            ## first plot data (no error bars)
            chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs, zorder=10)

            # display(plot_kwargs)

            ## then add error bars, reset the style cycle,
            ## and temporarily prefix column names with '_'
            ## to avoid duplicate legend entries
            # plot_kwargs['yerr'] = err
            # tmean.rename(columns=lambda x: '_'+x, inplace=True)
            # ax.set_prop_cycle(None)
            # chart = tmean.plot(fig=fig, ax=ax, **plot_kwargs, zorder=20) ## plot
            # tmean.rename(columns=lambda x: x[1:], inplace=True)

    #########################################
    ## decorate the plot
    #########################################

    ## x axis tick marks (proper centered tick label alignment)
    if kind == 'bar': chart.set_xticklabels(chart.get_xticklabels(), ha="center", rotation=0)
    else: ax.xaxis.set_major_locator(ticker.AutoLocator())

    ## y axis grid lines and minor tick marks
    plt.grid(axis='y', which='major', linestyle='-')
    plt.grid(axis='y', which='minor', linestyle='--')
    ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())

    ## handle possible logarithmic y axis ticks
    if logy: ax.yaxis.set_minor_locator(ticker.LogLocator(subs="all"))

    ## set x axis title
    if not xlabel or xlabel == '':
        ax.xaxis.label.set_visible(False)
    else:
        ax.xaxis.label.set_visible(True)
        ax.set_xlabel(xlabel)

    ## set y axis title
    if not ylabel or ylabel == '':
        ax.yaxis.label.set_visible(False)
    else:
        ax.yaxis.label.set_visible(True)
        ax.set_ylabel(ylabel)

    ## configure legend
    if legend: plt.legend(**legend_kwargs)



## todo: sort of "region" plot possible with sns line
## todo: hist, hist2d -- probably a good idea to go intentionally very light on features for now...
## todo: user plot function plugin capability in define_experiment... (user func that is supplied same sort of args as get_dataframe_and_call)
## todo: consistent map from all possible series to bar color + hatching (similar to map_series_to_pandas_line_style)
## todo: gstats output, i.e., integrate trial_to_plot(_strip|_grid) -- need some setbench "plugin" for this...? similar to how _user_experiment is a plugin in a sense... need extractors for a sequence of 1D data sources... to be plotted in a wraparound grid (facetgrid or simple subplots env i guess)... this would differ substantially in that we would NOT be pulling data from the sqlite database... this is just single column data...
## todo: timeline (again, a plugin of sorts... function to extract dataframe... NO sqlite integration, unless we construct a temporary database... would it make sense to do this? or is the structure of the data too trivial? maybe querying to limit time window / interval / types would be nice... temporary database it is then i guess... could be a good excuse to create a setbench module that cleanly collects such data in memory then dumps to sqlite? or is a c++ sqlite interface too annoying to setup? probably... better to just dump to file then parse in python...)
## todo: jupyternb for using the actual experiment definition framework (how to make this kind of tutorial code/integration work?)
## todo: anim -- could be "proper" matplotlib anims, or simpler gif production from static plots...
## todo: error bars on line plot somehow... seems VERY hard (at least without switching to matplotlib or maybe seaborn)... maybe a seaborn plot with "rays" or something?

def plot_df_bar(data, y, x='', series='', fig=None, ax=None, \
        title='', xlabel='', ylabel='', logx=False, logy=False, legend=True, **kwargs):

    mpl.rc('hatch', color='k', linewidth=2)
    #rcParams['font.size'] = 12
    plt.style.use('dark_background')

    plot_call_kwargs = dict({
          'figsize': (8, 5)
        , 'width': 0.75
        , 'edgecolor': 'black'
        , 'linewidth': 2
        , 'error_kw': dict(elinewidth=10, ecolor='red')
    })
    if 'plot_call_kwargs' in kwargs:
        plot_call_kwargs.update(kwargs['plot_call_kwargs'])

    legend_call_kwargs = dict()
    if 'legend_call_kwargs' in kwargs:
        legend_call_kwargs.update(kwargs['legend_call_kwargs'])

    plot_df(kind='bar', **locals())

    ## set hatch patterns for the bars
    if ax: _ax = ax
    else: _ax = plt.gca()
    bars = _ax.patches
    patterns =( 'x', '/', '//', 'O', 'o', '\\', '\\\\', '-', '+', ' ' )
    hatches = [p for p in patterns for i in range(len(data))]
    for bar, hatch in zip(bars, hatches):
        bar.set_hatch(hatch)



## returns pandas line plot kwarg: style
def map_series_to_pandas_line_style(distinct_series=''):
    styles=['^-c', 'o-r', 's-y', '+-g', 'x-m', 'v-b', '*-', 'X-', '|-', '.-', 'd-']
    if not distinct_series: return styles
    ret_style = dict()
    for i, series in zip(range(len(distinct_series)), distinct_series):
        ret_style[series] = styles[i % len(styles)]
        ## ensure mapping is the same even with a leading underscore,
        ## because we add an underscore when double plotting series
        ## to get proper error bars... (underscore prevents duplicate legend entries)
        ret_style['_'+series] = styles[i % len(styles)]
    return ret_style

def plot_df_line(data, y, x='', series='', fig=None, ax=None, \
        title='', xlabel='', ylabel='', logx=False, logy=False, legend=True, \
        style='', all_possible_series='', **kwargs):

    if not style: style = map_series_to_pandas_line_style(all_possible_series)
    # if not colormap: colormap = map_distinct_series_to_pandas_colormap(all_possible_series)
    # print(style)

    plot_call_kwargs = dict(
          figsize=(8, 5)
        , linewidth=2
        , style=style
    )
    if 'plot_call_kwargs' in kwargs:
        plot_call_kwargs.update(kwargs['plot_call_kwargs'])

    legend_call_kwargs = dict()
    if 'legend_call_kwargs' in kwargs:
        legend_call_kwargs.update(kwargs['legend_call_kwargs'])

    plt.style.use('dark_background')
    plot_df(kind='line', **locals())
