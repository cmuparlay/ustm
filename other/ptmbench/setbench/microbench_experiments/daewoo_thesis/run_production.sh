#!/bin/bash

# ../../tools/data_framework/run_experiment.py exp_vs_time.py -crdp
# ../../tools/data_framework/run_experiment.py exp_vs_threads.py -crdp
../../tools/data_framework/run_experiment.py exp_llheap_240.py -crdp
../../tools/data_framework/run_experiment.py exp_llheap_240amortized.py -crdp

zip -r data.zip data_vs_threads data_vs_time data_llheap_240 data_llheap_240amortized
tail output_log.txt
