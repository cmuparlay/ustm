import subprocess

with open("outTemp.txt") as f:
	for l in [line.rstrip().lstrip() for line in f]:
		l = (l.split(":"))[0]		
		subprocess.call(["cp", l, "failed_data_txts"])