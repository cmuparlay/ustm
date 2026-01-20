#!/usr/bin/python3

import numpy as np
import sys
import getopt
import os
import fileinput
import argparse

######################
## parse arguments
######################

parser = argparse.ArgumentParser(description='Produce a pandas bar plot from TWO COLUMN <label> <y> data (or ONE COLUMN <y> data) provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing lines of form <label> <y> (or lines of form <y>); if none specified then will use stdin. (if your data is not in this order, try using awk to easily shuffle columns...)')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default='out.png', help='output file with any image format extension such as .png or .svg; if none specified then plt.show() will be used')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--title-total', dest='title_total', action='store_true', help='add the total of all y-values to the title; if the title contains {} it will be replaced by the total; otherwise, the total will be appended to the end of the string')
parser.set_defaults(title_total=False)
parser.add_argument('--xfreq', dest='xfreq', type=int, default=1, help='number of x-axis labels consumed per label drawn')
parser.add_argument('--xtitle', dest='xtitle', default="", help='title for the x-axis')
parser.add_argument('--ytitle', dest='ytitle', default="", help='title for the y-axis')
parser.add_argument('--width', dest='width_inches', type=float, default=8, help='width in inches for the plot (at given dpi); default 8')
parser.add_argument('--height', dest='height_inches', type=float, default=6, help='height in inches for the plot (at given dpi); default 6')
parser.add_argument('--dpi', dest='dots_per_inch', type=int, default=100, help='DPI (dots per inch) to use for the plot; default 100')
parser.add_argument('--xnoaxis', dest='xnoaxis', action='store_true', help='disable the x-axis')
parser.set_defaults(xnoaxis=False)
parser.add_argument('--ynoaxis', dest='ynoaxis', action='store_true', help='disable the y-axis')
parser.set_defaults(ynoaxis=False)
parser.add_argument('--style-hooks', dest='style_hooks', default='', help='allows a filename to be provided that implements functions style_init(mpl), style_before_plotting(mpl, plot_kwargs_dict) and style_after_plotting(mpl). your file will be imported, and hooks will be added so that your functions will be called to style the mpl instance. note that plt/fig/ax can all be extracted from mpl.')
args = parser.parse_args()

# parser.print_usage()
if len(sys.argv) < 2:
    if sys.stdin.isatty():
        parser.print_usage()
        print('waiting on stdin for data...')

# print('args={}'.format(args))

######################
## load data
######################

x = []
y = []

one_col = False
two_col = False

i=0
# print(args.infile)
for line in args.infile:
    i=i+1
    line = line.rstrip('\r\n')
    if (line == '' or line == None):
        continue

    tokens = line.split(" ")
    if len(tokens) == 2:
        two_col = True
        x.append(tokens[0])
        y.append(float(tokens[1]))
    elif len(tokens) == 1:
        one_col = True
        x.append(i)
        y.append(float(tokens[0]))
    else:
        print("ERROR at line {}: '{}'".format(i, line))
        exit(1)

if not len(x):
    print("ERROR: no data provided, so no graph to render.")
    quit()

if one_col and two_col:
    print("ERROR: cannot supply both one-column and two-column data lines.")
    quit()

######################
## setup matplotlib
######################

import platform
WIN = (platform.system() == 'Windows' or 'CYGWIN' in platform.system())

import matplotlib as mpl
if WIN:
    mpl.use('TkAgg')
else:
    mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

if args.style_hooks != '':
    sys.path.append(os.path.dirname(args.style_hooks))
    module_filename = os.path.relpath(args.style_hooks, os.path.dirname(args.style_hooks)).replace('.py', '')
    import importlib
    mod_style_hooks = importlib.import_module(module_filename)
    mod_style_hooks.style_init(mpl)

else:
    plt.style.use('dark_background')
    plt.rcParams["figure.dpi"] = args.dots_per_inch

######################
## setup plot
######################

import pandas as pd

df = pd.DataFrame({'labels': x, 'values': y})
# print('data len={}'.format(len(df['labels'])))

if args.title_total:
    import locale
    locale.setlocale(locale.LC_ALL, '')
    sumstr = '{:n}'.format(sum(y))
    if '{}' in args.title:
        _title = args.title.format(sumstr)
    else:
        _title = '{}{}'.format(args.title, sumstr)
    # _title = "{}{}".format(args.title, sum(y))
elif args.title != '':
    _title = args.title
else:
    _title = ''

plot_kwargs = dict({
      'x': 'labels'
    , 'y': 'values'
    , 'figsize': (args.width_inches, args.height_inches)
})

fig, ax = plt.subplots()
if args.style_hooks != '': mod_style_hooks.style_before_plotting(mpl, plot_kwargs)

if _title == '':
    df.plot.bar(ax=ax, fig=fig, **plot_kwargs)
else:
    plot_kwargs['title'] = _title
    df.plot.bar(ax=ax, fig=fig, **plot_kwargs)

######################
## y axis major, minor
######################

ax = plt.gca()

ax.grid(which='major', axis='y', linestyle='-', color='lightgray')

ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='y', linestyle='dotted', color='gray')

ax.get_legend().remove()

######################
## x axis major, minor
######################

# ax.grid(which='major', axis='x', linestyle='-', color='lightgray')

# ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())
# ax.xaxis.set_major_locator(ticker.AutoLocator())

# ax.grid(which='minor', axis='x', linestyle='dotted', color='gray')


ticks = ax.xaxis.get_ticklocs()
ticklabels = [l.get_text() for l in ax.xaxis.get_ticklabels()]
ax.xaxis.set_ticks(ticks[::args.xfreq])
ax.xaxis.set_ticklabels(ticklabels[::args.xfreq])

if args.xnoaxis:
    plt.setp(ax.get_xticklabels(), visible=False)
if args.ynoaxis:
    plt.setp(ax.get_yticklabels(), visible=False)

if args.xtitle == "" or args.xtitle == None:
    ax.xaxis.label.set_visible(False)
else:
    ax.xaxis.label.set_visible(args.xtitle)

if args.ytitle == "" or args.ytitle == None:
    ax.yaxis.label.set_visible(False)
else:
    ax.yaxis.label.set_visible(args.ytitle)

######################
## do the plot
######################

plt.tight_layout()

if args.style_hooks != '': mod_style_hooks.style_after_plotting(mpl)

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure %s" % args.outfile.name)
    plt.savefig(args.outfile.name)
