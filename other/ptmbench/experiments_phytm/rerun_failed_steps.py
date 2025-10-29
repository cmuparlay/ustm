import subprocess
import re

stepsFile="outTemp.txt"
# stepsFile="ablation_files.txt"
# stepsFile="abtree_file.txt"
# stepsFile="hashmap_files.txt"

steps = []
with open(stepsFile) as f:
	for l in [line.rstrip() for line in f]:
		l = (l.split(":"))[0]		
		n = re.findall(r'\d+', l)[0]
		steps.append(n)
		print(n)

if len(steps) > 0:
	# ../setbench/tools/data_framework/run_experiment.py _user_experiment.py --rerun-steps
	callCmd = ["../setbench/tools/data_framework/run_experiment.py", "_user_experiment.py", "--rerun-steps"]
	for s in steps:
		callCmd.append(s)
	subprocess.call(callCmd)


# nums = []
# with open("outTemp2.txt") as f:
# 	for l in [line.rstrip() for line in f]:
# 		l = (l.split(":"))[0]		
# 		print(l)
# 		n = re.findall(r'\d+', l)
# 		if len(n) > 0:
# 			print(n[0])
# 			nums.append(n[0])
# nums = sorted(list(set(nums)))
# print(nums)

# callCmd = ["../setbench/tools/data_framework/run_experiment.py", "_user_experiment.py", "--rerun-steps"]
# for n in nums:
# 	callCmd.append(n)
# subprocess.call(callCmd)