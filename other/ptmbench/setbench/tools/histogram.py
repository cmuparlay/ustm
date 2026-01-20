#!/usr/bin/python3

import numpy as np
import platform
import sys, getopt
import fileinput
import argparse

parser = argparse.ArgumentParser(description='Plot a 1-dimensional histogram from SINGLE COLUMN data provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing values to plot, ONE PER LINE; if none specified then will use stdin')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default=None, help='output file with any image format extension such as .png or .svg; if none specified then plt.show() will be used')
parser.add_argument('-n', dest='numBins', type=int, default=10, help='number of histogram bins to plot (default 10)')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--title-total', dest='title_total', action='store_true', help='add the total of all y-values to the title; if the title contains {} it will be replaced by the total; otherwise, the total will be appended to the end of the string')
parser.set_defaults(title_total=False)
parser.add_argument('--width', dest='width_inches', type=float, default=8, help='width in inches for the plot (at given dpi); default 8')
parser.add_argument('--height', dest='height_inches', type=float, default=6, help='height in inches for the plot (at given dpi); default 6')
parser.add_argument('--dpi', dest='dots_per_inch', type=float, default=100, help='DPI (dots per inch) to use for the plot; default 100')
parser.add_argument('--logscale', dest='useLogscale', action='store_true', help='use a logarithmic axis')
parser.set_defaults(useLogscale=False)
parser.add_argument('--all-log-ticks', dest='logscaleAllticks', action='store_true', help='force the logarithmic y-axis to include all minor ticks')
parser.set_defaults(logscaleAllticks=False)
parser.add_argument('--xticks-multiple', dest='xticksMultipleOfBinSize', action='store_true', help='force the x-axis ticks to be a multiple of the bin size')
parser.add_argument('--cumulative', dest='cumulative', action='store_true', help='histogram is computed where each bin gives the counts in that bin plus all bins for SMALLER values')
parser.set_defaults(cumulative=False)
parser.add_argument('--rcumulative', dest='cumulative', action='store_const', const=-1, help='histogram is computed where each bin gives the counts in that bin plus all bins for LARGER values')

# parser.add_argument('data', metavar='VALUE', type=float, nargs='*', help='a value to plot (if not using stdin or an input file)')
args = parser.parse_args()

# parser.print_usage()
if len(sys.argv) < 2:
    parser.print_usage()
    print('waiting on stdin for histogram data...')

# print('args={}'.format(args))

WIN = (platform.system() == 'Windows' or 'CYGWIN' in platform.system())

import matplotlib as mpl
if WIN:
    mpl.use('TkAgg')
else:
    mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
plt.style.use('dark_background')

fig = plt.figure(figsize=(args.width_inches, args.height_inches), dpi=args.dots_per_inch)

data = []

i=0
# print(args.infile)
for line in args.infile:
    i=i+1
    line = line.rstrip('\r\n')
    if len(line.split(" ")) != 1:
        print("ERROR at line {}: '{}'".format(i, line))
        exit(1)

    if (line != '' and line != None):
        data.append(float(line))

######################
## plot data
######################

print('data len={} numBins={}'.format(len(data), args.numBins))
n, bins, patches = plt.hist(data, bins=args.numBins, log=args.useLogscale, color='red', cumulative=args.cumulative)
# print("cumulative={}".format(args.cumulative))

######################
## plot title
######################

if args.title_total:
    import locale
    locale.setlocale(locale.LC_ALL, '')
    sumstr = '{:n}'.format(sum(data))
    if '{}' in args.title:
        _title = args.title.format(sumstr)
    else:
        _title = '{}{}'.format(args.title, sumstr)
    # _title = "{}{}".format(args.title, sum(y))
elif args.title != '':
    _title = args.title
else:
    _title = ''

if _title != '':
    plt.title(_title)

######################
## y axis major, minor
######################

ax = plt.gca()

ax.grid(which='major', axis='y', linestyle='-', color='lightgray')

if args.useLogscale:
    if args.logscaleAllticks:
        ax.yaxis.set_minor_locator(ticker.LogLocator(subs="all"))
    else:
        ax.yaxis.set_minor_locator(ticker.LogLocator())
else:
    ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='y', linestyle='dotted', color='gray')

######################
## x axis major, minor
######################

ax.grid(which='major', axis='x', linestyle='-', color='lightgray')

# for p in patches: print(p)
if (len(patches) < 1):
    print("ERROR: no bars to plot...")
    exit(1)
w = patches[0].get_width()
# print('bar width={}'.format(w))

if args.xticksMultipleOfBinSize:
    sizeMajorTick = max(1, (args.numBins // 10)) * w
    ax.xaxis.set_major_locator(ticker.MultipleLocator(sizeMajorTick))

if args.xticksMultipleOfBinSize:
    ax.xaxis.set_minor_locator(ticker.MultipleLocator(w))
else:
    ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())

ax.grid(which='minor', axis='x', linestyle='dotted', color='gray')

######################
## do the plot
######################

plt.tight_layout()
# ax.set_xticks(range(int(plt.xlim()[0]), int(plt.xlim()[1]), int(plt.xlim()[1]-plt.xlim()[0])//args.numBins))

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure image %s\n" % args.outfile.name)
    plt.savefig(args.outfile.name)
