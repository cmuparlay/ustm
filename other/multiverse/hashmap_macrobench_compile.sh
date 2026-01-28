#!/bin/bash

make -C ./setbench/macrobench workload=YCSB data_structure_name=hashmap data_structure_path=../../ds_dcotl/hashmap extra_src_dirs=../../dcotl/
make -C ./setbench/macrobench workload=YCSB 