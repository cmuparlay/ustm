#!/usr/bin/python3

import numpy as np
import platform
import sys, getopt
import fileinput
import argparse

parser = argparse.ArgumentParser(description='Plot a 2-dimensional histogram from THREE COLUMN data provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing values to plot, TWO PER LINE in format "<x> <y> <weight>\n"; if no file is specified then stdin will be used')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default=None, help='output file with any image format extension such as .png or .svg; if no file is specified then plt.show() will be used')
# parser.add_argument('-n', dest='numBins', type=int, default=-1, help='number of histogram bins to plot (defaults to max x - min x + 1)')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--width', dest='width_inches', type=int, default=8, help='width in inches for the plot (at given dpi); default 8')
parser.add_argument('--height', dest='height_inches', type=int, default=6, help='height in inches for the plot (at given dpi); default 6')
parser.add_argument('--dpi', dest='dots_per_inch', type=int, default=100, help='DPI (dots per inch) to use for the plot; default 100')
parser.add_argument('--logscale', dest='useLogscale', action='store_true', help='use a logarithmic color scale')
parser.set_defaults(useLogscale=False)
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
dataw = []

i=0
# print(args.infile)
for line in args.infile:
    i=i+1
    line = line.rstrip('\r\n')
    tokens = line.split(" ")
    if len(tokens) != 3:
        print("ERROR at line {}: '{}'".format(i, line))
        exit(1)

    x=int(tokens[0])
    y=int(tokens[1])
    w=int(tokens[2])
    datax.append(x)
    datay.append(y)
    dataw.append(w)
    # print('appending {} {} {}'.format(x, y, w))

######################
## plot data
######################

maxx = max(datax)
minx = min(datax)
maxy = max(datay)
miny = min(datay)
maxw = max(dataw)
minw = min(dataw)

# binx = maxx - minx + 1
# biny = maxy - miny + 1
# if args.numBins == -1:
#     if binx != biny:
#         print()
#         print('################################################################################')
#         print('## WARNING: number of x bins {} does not match number of y bins {}!'.format(binx, biny))
#         print('##          this may cause "beats" or moire patterns in the histogram')
#         print('##          which may cause erroneous conclusions unless extreme care is taken!')
#         print('##')
#         print('##          if you have never seen this before, try plotting a UNIFORM histogram')
#         print('##          with the same x/y ranges as your current data!')
#         print('################################################################################')
#         print()
#     else:
#         # args.numBins = 10
#         args.numBins = binx
# else:
#     if binx != args.numBins:
#         print()
#         print('################################################################################')
#         print('## WARNING: number of bins provided {} does not match the range of x values {}!'.format(args.numBins, binx))
#         print('##          this may cause "beats" or moire patterns in the histogram')
#         print('##          which may cause erroneous conclusions unless extreme care is taken!')
#         print('##')
#         print('##          if you have never seen this before, try plotting a UNIFORM histogram')
#         print('##          with the same x/y ranges as your current data!')
#         print('################################################################################')
#         print()

print('xlen={} ylen={} minx={} maxx={} miny={} maxy={}'.format(len(datax), len(datay), minx, maxx, miny, maxy))
# print('xlen={} ylen={} bins={}'.format(len(datax), len(datay), args.numBins))
h, xedges, yedges, im = plt.hist2d(datax, datay, weights=dataw
        , bins=[maxx-minx+1, maxy-miny+1]
        # , bins=np.arange(args.numBins+1)-0.5
        , norm=(mpl.colors.LogNorm() if args.useLogscale else mpl.colors.Normalize())
        , vmin=0
        , cmap='plasma')

######################
## legend
######################

fig.colorbar(im)

######################
## do the plot
######################

if args.title != '':
    plt.title(args.title)

plt.tight_layout()

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure image %s" % args.outfile.name)
    plt.savefig(args.outfile.name)
