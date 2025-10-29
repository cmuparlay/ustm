#!/bin/bash

(echo "socket core hwthread" ; cat /proc/cpuinfo | grep -E "processor|core id|physical id" | tr -d "a-zA-Z:" | awk '{ if ((NR % 3) == 1) { last2 = $1; } else if ((NR % 3) == 2) { last = $1; } else { print last2, last, $1 }}' | awk '{print $2, $3, $1}') #| column -t
