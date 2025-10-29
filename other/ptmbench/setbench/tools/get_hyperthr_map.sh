#!/bin/bash

# cat /proc/cpuinfo | grep -E 'processor|core id|physical id' | tr "\n" "~" | sed -E 's/processor\s*:\s([0-9]*)~physical id\s*:\s([0-9]*)~core id\s*:\s([0-9]*)/\1 \2_\3/g' | tr "~" "\n" | awk '{ a[$2] = a[$2] " " $1 }END{ for (i in a) { print a[i] } }' | awk '{ print $1, $2 ; print $2, $1 ;}' | sort -n
export PATH=$PATH:.
get_cpuinfo_sockets_cores_threads.sh | tail +2 | awk '{i=$1 "_" $2; hts[i]=hts[i] " " $3} END {for (i in hts) print i, hts[i]}' | awk '{print $2, $3; print $3, $2}' | sort -n
