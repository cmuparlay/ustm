#!/bin/bash

echo "ASLR options:"
echo "0 – No randomization. Everything is static."
echo "1 – Conservative randomization. Shared libraries, stack, mmap(), VDSO and heap are randomized."
echo "2 – Full randomization. In addition to elements listed in the previous point, memory managed through brk() is also randomized."
echo ""


old=`cat /proc/sys/kernel/randomize_va_space`

echo "Current ASLR is set to:"
echo $old
echo ""

if [[ $old == 2 ]]; then
	echo "Toggling ASLR Off"
	echo 0 | sudo tee /proc/sys/kernel/randomize_va_space
else 
	echo "Toggling ASLR On"
	echo 2 | sudo tee /proc/sys/kernel/randomize_va_space
fi

