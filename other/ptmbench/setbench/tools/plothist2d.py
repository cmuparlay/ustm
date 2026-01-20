#!/usr/bin/python3

import numpy as np
import platform
import sys, getopt
import fileinput
import argparse

parser = argparse.ArgumentParser(description='Plot a 2-dimensional histogram from TWO COLUMN data provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing values to plot, TWO PER LINE in format "<x> <y>\n"; if no file is specified then stdin will be used')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default=None, help='output file with any image format extension such as .png or .svg; if no file is specified then plt.show() will be used')
parser.add_argument('-n', dest='numBins', type=int, default=-1, help='number of histogram bins to plot (defaults to max x - min x + 1)')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--width', dest='width_inches', type=float, default=8, help='width in inches for the plot (at given dpi); default 8')
parser.add_argument('--height', dest='height_inches', type=float, default=6, help='height in inches for the plot (at given dpi); default 6')
parser.add_argument('--dpi', dest='dots_per_inch', type=float, default=100, help='DPI (dots per inch) to use for the plot; default 100')
parser.add_argument('--logscale', dest='useLogscale', action='store_true', help='use a logarithmic color scale')
parser.set_defaults(useLogscale=False)
parser.add_argument('--title-total', dest='title_total', action='store_true', help='append the total of all y-values to the title')
parser.set_defaults(title_total=False)

# parser.add_argument('--ticks-multiple', dest='xticksMultipleOfBinSize', action='store_true', help='force the x-axis ticks to be a multiple of the bin size')
# parser.add_argument('--cumulative', dest='cumulative', action='store_true', help='histogram is computed where each bin gives the counts in that bin plus all bins for SMALLER values')
# parser.add_argument('--rcumulative', dest='cumulative', action='store_const', const=-1, help='histogram is computed where each bin gives the counts in that bin plus all bins for LARGER values')
# parser.set_defaults(cumulative=False)
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
#fig = plt.figure()

datax = []
datay = []

i=0
# print(args.infile)
for line in args.infile:
    i=i+1
    line = line.rstrip('\r\n')
    tokens = line.split(" ")
    if len(tokens) != 2:
        print("ERROR at line {}: '{}'".format(i, line))
        exit(1)

    x=int(tokens[0])
    y=int(tokens[1])
    datax.append(x)
    datay.append(y)
    # print('appending {} {}'.format(x, y))

######################
## plot data
######################

maxx = max(datax)
minx = min(datax)
maxy = max(datay)
miny = min(datay)
binx = maxx - minx + 1
biny = maxy - miny + 1
if args.numBins == -1:
    if binx != binx:
        print()
        print('################################################################################')
        print('## WARNING: number of x bins {} does not match number of y bins {}!'.format(binx, biny))
        print('##          this may cause "beats" or moire patterns in the histogram')
        print('##          which may cause erroneous conclusions unless extreme care is taken!')
        print('##')
        print('##          if you have never seen this before, try plotting a UNIFORM histogram')
        print('##          with the same x/y ranges as your current data!')
        print('################################################################################')
        print()
    else:
        # args.numBins = 10
        args.numBins = binx
else:
    if binx != args.numBins:
        print()
        print('################################################################################')
        print('## WARNING: number of bins provided {} does not match the range of x values {}!'.format(args.numBins, binx))
        print('##          this may cause "beats" or moire patterns in the histogram')
        print('##          which may cause erroneous conclusions unless extreme care is taken!')
        print('##')
        print('##          if you have never seen this before, try plotting a UNIFORM histogram')
        print('##          with the same x/y ranges as your current data!')
        print('################################################################################')
        print()

print('xlen={} ylen={} bins={}'.format(len(datax), len(datay), args.numBins))
h, xedges, yedges, im = plt.hist2d(datax, datay, bins=np.arange(args.numBins+1)-0.5, norm=(mpl.colors.LogNorm() if args.useLogscale else mpl.colors.Normalize()), cmap='hot')

######################
## legend
######################

fig.colorbar(im)

# ######################
# ## y axis major, minor
# ######################

ax = plt.gca()

# xuniq = list(set(datax))
# # xuniq.append(max(xuniq)+1)
# yuniq = list(set(datay))
# # yuniq.append(max(yuniq)+1)
# ax.set_yticks(np.arange(max(yuniq)+1) - 0.5)
# ax.axis([min(xuniq), max(xuniq), min(yuniq), max(yuniq)+1])
# ax.set_yticklabels(yuniq, fontdict={'verticalalignment': 'center'})


# ax.grid(which='major', axis='y', linestyle='-', color='lightgray')

# ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
# ax.grid(which='minor', axis='y', linestyle='dotted', color='gray')

# ######################
# ## x axis major, minor
# ######################

# ax.grid(which='major', axis='x', linestyle='-', color='lightgray')

# # for p in patches: print(p)
# if (len(patches) < 1):
#     print("ERROR: no bars to plot...")
#     exit(1)
# w = patches[0].get_width()
# print('bar width={}'.format(w))

# if args.xticksMultipleOfBinSize:
#     sizeMajorTick = max(1, (args.numBins // 10)) * w
#     ax.xaxis.set_major_locator(ticker.MultipleLocator(sizeMajorTick))

# if args.xticksMultipleOfBinSize:
#     ax.xaxis.set_minor_locator(ticker.MultipleLocator(w))
# else:
#     ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())

# ax.grid(which='minor', axis='x', linestyle='dotted', color='gray')

######################
## do the plot
######################

if args.title_total:
    import locale
    locale.setlocale(locale.LC_ALL, '')
    sumstr = '{:n}'.format(sum(datay))
    plt.title('{}{}'.format(args.title, sumstr))
    # sumstr = '{}{:,}'.format(args.title, sum(datay))
    # print("sumstr={}".format(sumstr))
    # plt.title('{}{}'.format(args.title, sum(datay)))
elif args.title != '':
    plt.title(args.title)

plt.tight_layout()
# ax.set_xticks(range(int(plt.xlim()[0]), int(plt.xlim()[1]), int(plt.xlim()[1]-plt.xlim()[0])//args.numBins))

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure image %s" % args.outfile.name)
    plt.savefig(args.outfile.name)
