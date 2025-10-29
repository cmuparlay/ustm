#!/usr/bin/python3

import numpy as np
import platform
import sys, getopt
import fileinput
import argparse

######################
## parse arguments
######################

parser = argparse.ArgumentParser(description='Produce a single-curve (x,y)-line plot from TWO COLUMN data (or ONE COLUMN y-data) provided via a file or stdin.')
parser.add_argument('-i', dest='infile', type=argparse.FileType('r'), default=sys.stdin, help='input file containing lines of form <x> <y> (or lines of form <y>); if none specified then will use stdin')
parser.add_argument('-o', dest='outfile', type=argparse.FileType('w'), default='out.png', help='output file with any image format extension such as .png or .svg; if none specified then plt.show() will be used')
parser.add_argument('-t', dest='title', default="", help='title string for the plot')
parser.add_argument('--title-total', dest='title_total', action='store_true', help='add the total of all y-values to the title; if the title contains {} it will be replaced by the total; otherwise, the total will be appended to the end of the string')
parser.set_defaults(title_total=False)
parser.add_argument('--no-y-min', dest='no_ymin', action='store_true', help='eliminate the minimum y-axis value of 0')
parser.set_defaults(no_ymin=False)
args = parser.parse_args()

# parser.print_usage()
if len(sys.argv) < 2:
    if sys.stdin.isatty():
        parser.print_usage()
        print('waiting on stdin for data...')

# print('args={}'.format(args))

WIN = (platform.system() == 'Windows' or 'CYGWIN' in platform.system())

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

        ## if the first col is text, let's assume it's really single column y-data
        try:
            x.append(int(tokens[0]))
        except:
            x.append(i)

        y.append(int(tokens[1]))

    elif len(tokens) == 1:
        one_col = True
        x.append(int(i))
        y.append(int(tokens[0]))

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

import matplotlib as mpl
if WIN:
    mpl.use('TkAgg')
else:
    mpl.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
plt.style.use('dark_background')

######################
## setup plot
######################

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

dots_per_inch = 200
height_inches = 5
width_inches = 12

fig = plt.figure(figsize=(width_inches, height_inches), dpi=dots_per_inch)

if _title: plt.title(_title)

#print('data len={}'.format(len(x)))
plt.plot(x, y, color='red')

if not args.no_ymin: plt.ylim(bottom=0)

######################
## y axis major, minor
######################

ax = plt.gca()

ax.grid(which='major', axis='y', linestyle='-', color='lightgray')

ax.yaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='y', linestyle='dotted', color='gray')

######################
## x axis major, minor
######################

ax.grid(which='major', axis='x', linestyle='-', color='lightgray')

ax.xaxis.set_minor_locator(ticker.AutoMinorLocator())
ax.grid(which='minor', axis='x', linestyle='dotted', color='gray')

######################
## do the plot
######################

plt.tight_layout()

if args.outfile == None:
    if WIN:
        mng = plt.get_current_fig_manager()
        mng.window.state('zoomed')
    plt.show()
else:
    print("saving figure %s" % args.outfile.name)
    plt.savefig(args.outfile.name)
