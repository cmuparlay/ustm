#!/bin/bash

cd ..
./compile_trinity_quadra.sh
./compile_phytm.sh
./compile_spht.sh
cd -
../setbench/tools/data_framework/run_experiment.py _user_experiment.py -rd
