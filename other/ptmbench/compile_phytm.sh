#!/bin/bash

# make -j new-ds-reclaim-phytm partial_algorithm_names="phytm_ hytm3_"  PTM_ALGS="phytm"
# make -j new-ds-reclaim-phytm partial_algorithm_names="BTree"  PHYTM_ALGS="phytm1 phytm1_fgl phytm1_colocated_locks phytm1_fgl_always_sorting phytm1_fgl_and_rdwrset_sorting"
make -j new-ds-reclaim-phytm partial_algorithm_names="abtree hashmap"  