This repository contains the code used to generate the benchmarks results from the paper Persistent HyTM via Fast Path Fine-Grained Locking

Our NV-HALT code can be found in the phytmlib directory

For NV-HALT STAMP benchmarks checkout the stamp-build branch

# NV-HALT
The NV-HALT code can be found in the /phytmlib directory. The actual TM header is /phytmlib/phytmCommon/tm.h

# Configuration:
Specific NV-HALT parameters can be adjusted in /phytmlib/phytmCommon/config.h

If you wish to compile without the use of NVRAM define the DISABLE_PMEM macro.
If you wish to compile without the use of HTM define the DISABLE_HTM macro.

Our code is setup to run on a 2 socket system, however you can run on a single socket system.
The NUMA node of each thread is automatically detected at runtime.
If you want to run on a system with more than 2 NUMA nodes you will need to adjust the memory manager.
You will need to setup proper paths for your PMEM files. Redefine  PMEM_FILE_PATH_NUMA0 and PMEM_FILE_PATH_NUMA1

# Compiling:
All of the compiler targets are defined in Makefile.
Most of the parameters can be left on the defaults.
You may wish to change the MAX_THREADS_POW2 macro.

To compile all of the TMs run compile.sh
There are also individual compile_tm_name shell scripts that you can use to compile a single TM.

# Experiments:
All of the experiments can be run via Setbench tools.
The experimental setup is defined in /experiments_phytm/_user_experiment.py
After changing the setup to your liking simply run /experiments_phytm/run.sh