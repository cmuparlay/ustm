#!/bin/bash

make clean -C ./setbench/macrobench workload=YCSB data_structure_name=hashmap data_structure_path=../../ds_dcotl/hashmap extra_src_dirs=../../dcotl/
make clean -C ./setbench/macrobench workload=YCSB

rm ./setbench/macrobench/bin/dcotl -R