import os
import subprocess
import sys
import socket
import csv
from datetime import date

def getOption(str) :                                                                                
    return sys.argv.count(str) > 0                                                                  
                                                                                                    
def getArg(str, default) :                                                                          
  a = sys.argv                                                                                      
  l = len(a)                                                                                        
  for i in range(1, l) :                                                                            
    if (a[i] == str and  (i+1 != l)) :                                                              
        return sys.argv[i+1]                                                                        
  return default

rounds = int(getArg("-r", 0));

time = float(getArg("-t", 0.0));

limit = int(getArg("-limit", 600));

shuffle = getOption("-shuffle");

test_only = getOption("-test")

today = date.today().strftime("%m_%d_%y")
hostname = socket.gethostname()

print(hostname)
print(today)

def geometric_mean(vals) :
    product = 1.0;
    for x in vals : product = product * x
    return pow(product, 1.0 / len(vals))

def detectCPUs():
    """
     Detects the number of CPUs on a system. Cribbed from pp.
     """
    # Linux, Unix and MacOS:
    if hasattr(os, "sysconf"):
       if "SC_NPROCESSORS_ONLN" in os.sysconf_names :
           # Linux & Unix:
           ncpus = os.sysconf("SC_NPROCESSORS_ONLN")
           if isinstance(ncpus, int) and ncpus > 0:
               return ncpus
       else: # OSX:
           return int(os.popen2("sysctl -n hw.ncpu")[1].read())
    # Windows:
    if os.environ.has_key("NUMBER_OF_PROCESSORS"):
           ncpus = int(os.environ["NUMBER_OF_PROCESSORS"]);
           if ncpus > 0:
               return ncpus
    return 1 # Default

maxcpus = detectCPUs()

class parameters :
    time = 1
    rounds = 1
    summary = False
    use_batch = True
    upsert = False
    zipfians = []
    file_suffix = ""
    lists = []
    lists_ro = []
    list_sizes = []
    trees = []
    trees_ro = []
    tree_sizes = []
    suffixes_all = [""]
    suffixes_ro = []
    mix_percents = [[5,0,0,0]]
    trans_sizes = [0]
    processors = [maxcpus-1]

accumulate_throughputs = []

def runstring(op, outfile) :
    use_outfile = not(test_only) and len(outfile) > 0
    if use_outfile :
        cmd = op + " >> " + outfile
    else :
        cmd = op
    os.system("echo \"" + cmd + "\"")
    x = os.system(cmd)
    if (x) :
        if (os.WEXITSTATUS(x) == 0) : raise NameError("  aborted: " + op)
        if use_outfile :
            os.system("echo \"Failed: " + op + "\" >> " + outfile)
        os.system("echo Failed")

def runstring_with_output(op, outfile) :
    use_outfile = not(test_only) and len(outfile) > 0
    if use_outfile :
        cmd = op
    else :
        cmd = op
    os.system("echo \"" + cmd + "\"")
    [status,rstring] = subprocess.getstatusoutput(cmd)
    if use_outfile :
        os.system("echo \"" + rstring + "\"" + " >> " + outfile)
    else :
        os.system("echo \"" + rstring + "\"")
    if (status) :
#        if (os.WEXITSTATUS(x) == 0) : raise NameError("  aborted: " + op)
        if use_outfile :
            os.system("echo \"Failed: " + op + "\" >> " + outfile)
        os.system("echo Failed")
    return rstring

GEO_CSV_PATH = "../../geo_throughputs.csv"
CSV_PATH = "../../throughputs.csv"

def runtest(test,procs,n,z,mix,ts,params,filename) :
    num_threads = max(procs,maxcpus)
    if mix[1] > 0: str_mfind = "-mfind "
    else : str_mfind = ""
    str_proc = "-p " + str(procs) + " "
    str_mix = "-u " + str(mix[0]) + " -rqthreads " + str(mix[2]) + " "
    str_rs = "-rs " + str(mix[3]) + " "
    str_zipfians = "-z " + str(z) + " "
    str_trans_size = "-trans " + str(ts) + " "
    num_rounds = rounds
    if (num_rounds == 0) :
        num_rounds = params.rounds
    str_rounds = "-r " + str(num_rounds) + " "
    run_time = time
    if (run_time == 0.0) : run_time = params.time
    str_time = "-t " + str(run_time) + " "
    str_n = "-n " + str(n) + " "
    if shuffle : str_other = "-shuffle "
    else : str_other = ""
    tlimit = " timelimit -t " + str(limit)
    if (params.upsert) : str_other = str_other + "-upsert "
    str_csv = f"-csv {CSV_PATH} "
    str_geo_csv = f"-geo_csv {GEO_CSV_PATH}"
    full_str = "PARLAY_NUM_THREADS=" + str(num_threads) + tlimit + " numactl -i all ./" + test + " " + str_time + str_rounds + str_n + str_mfind + str_mix + str_rs + str_proc + str_zipfians + str_trans_size + str_other # + str_csv + str_geo_csv
    if params.summary :
        rstring = runstring_with_output(full_str, filename)
        global accumulate_throughputs
        accumulate_throughputs += [float(rstring.split(",")[-1])]
    else :
        runstring(full_str, filename)
        
def flatten(l) :
    return ",".join(str(s) for s in l)

def runtest_batch(test,procs,n,z,mix,ts,params,filename) :
    num_threads = max(procs,maxcpus)
    str_mfind = ""
    str_proc = "-p " + str(procs) + " "
    str_rs = ""
    
    str_mix = "-u " + flatten([x[0] for x in mix]) + " "
    str_zipfians = "-z " + flatten(z) + " "
    str_trans_size = "-trans " + flatten(ts) + " "
    str_n = "-n " + flatten(n) + " "

    num_rounds = rounds
    if (num_rounds == 0) :
        num_rounds = params.rounds
    str_rounds = "-r " + str(num_rounds) + " "
    run_time = time
    if (run_time == 0.0) : run_time = params.time
    str_time = "-tt " + str(run_time) + " "
    if shuffle : str_other = "-shuffle "
    else : str_other = ""
    tlimit = " timelimit -t " + str(limit)
    if (params.upsert) : str_other = str_other + "-upsert "
    str_csv = f"-csv {CSV_PATH} "
    str_geo_csv = f"-geo_csv {GEO_CSV_PATH}"
    full_str = "PARLAY_NUM_THREADS=" + str(num_threads) + tlimit + " numactl -i all ./" + test + " " + str_time + str_rounds + str_n + str_mfind + str_mix + str_rs + str_proc + str_zipfians + str_trans_size + str_other # + str_csv + str_geo_csv
    runstring(full_str, filename)

def mean_of_throughputs(name, outfile) :
    use_outfile = not(test_only) and len(outfile) > 0
    global accumulate_throughputs
    mean = round(10*geometric_mean(accumulate_throughputs))/10
    accumulate_throughputs = []
    lstr = "... " + name + ": MEAN OF THROUGHPUTS: " + str(mean)
    if use_outfile :
        os.system("echo \"" + lstr + "\"" + " >> " + outfile)
    else :
        os.system("echo \"" + lstr + "\"")


def run_tests(tests, sizes, params, filename) :
    global limit
    if (params.use_batch) : limit = 10 * limit
    str_csv = f"-csv {CSV_PATH}"
    str_geo_csv = f"-geo_csv {GEO_CSV_PATH}"
    runstring("PARLAY_NUM_THREADS=" + str(num_threads) + tlimit + " numactl -i all ./" + test + " " + str_time + str_rounds + str_n + str_mfind + str_mix + str_rs + str_proc + str_zipfians + str_trans_size + str_other, filename)

def run_tests(tests, sizes, params, filename):

    headers = [
        "Command",
        "Update Percent",
        "Transaction Size",
        "Range Size",
        "Size",
        "Threads", 
        "Zipfian",
        "Percent Aborts",
        "Throughout (MOps)"
    ]

    with open(CSV_PATH, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(headers)

    geo_headers = [
        "Command",
        "Throughput"
    ]

    with open(GEO_CSV_PATH, mode='w', newline='') as file:
        writer = csv.writer(file)
        writer.writerow(geo_headers)

    for test in tests :
        for suffix in params.suffixes_all:
            if (params.use_batch) :
                for p in params.processors :
                    runtest_batch(test + suffix, p, sizes, params.zipfians, params.mix_percents, params.trans_sizes, params, filename)
            else :
                for n in sizes :
                    for mix in params.mix_percents :
                        for ts in params.trans_sizes :
                            for z in params.zipfians :
                                for p in params.processors :
                                    runtest(test + suffix, p, n, z, mix, ts, params, filename)
                if params.summary:
                    mean_of_throughputs(test + suffix, filename)

def run_all(params) :
    os.makedirs("../../../timings", exist_ok=True)
    filename = "../../../timings/" + hostname[0:5] + "_" + params.file_suffix + "_" + today
    if test_only :
        params.time = .2
        params.zipfians = [.99]
        params.tree_sizes = [1,1000,100000]
        params.list_sizes = [1, 100]
        params.trans_sizes = [16]
        params.mix_percents = [[50,0,0,0]]
        params.processors = [maxcpus]
    run_tests(params.trees, params.tree_sizes, params, filename)
    run_tests(params.lists, params.list_sizes, params, filename)            
                    
def compile_clear(file_suffix) :
    os.system("make -j " + str(maxcpus))
    filename = "../../../timings/" + hostname[0:5] + "_" + file_suffix + "_" + today
    if os.path.exists(filename) :
        os.remove(filename)
    runstring("git rev-parse --short HEAD", filename)
