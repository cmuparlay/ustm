#/bin/base 

rm failed_data_txts/*.txt

python move_failed_txts.py
python rerun_failed_steps.py

