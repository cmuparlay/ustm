#!/bin/bash

python ../setbench/tools/data_framework/run_experiment.py _user_experiment.py --database | grep -e 'field "total_throughput" with value "0"' | cut -d":" -f1 > outTemp.txt
python ../setbench/tools/data_framework/run_experiment.py _user_experiment.py --database | grep -e 'field "elapsed_millis" with value ""' | cut -d":" -f1 >> outTemp.txt
rm failed_data_txts -R
mkdir failed_data_txts
python move_failed_txts.py