#!/bin/bash

# lscpu | grep -E "NUMA node[0-9]+ CPU" | cut -d":" -f2 | tr -d " " | tr "," " " | sed 's/-/../g' | sed 's/^/{/g' | sed 's/$/}/g' | sed 's/ /} {/g' | while read line ; do cmd="for x in $line ; do echo -n \" \$x\" ; done" ; socket=`eval "$cmd"` ; echo $socket ; done

lscpu \
        | grep -E "NUMA node[0-9]+ CPU" | cut -d":" -f2 | tr -d " " \
        | tr "," " " | sed 's/-/../g' | sed 's/^/{/g' | sed 's/$/}/g' | sed 's/ /} {/g' \
        | while read line ; do 
            cmd="for x in $line ; do echo -n \" \$x\" ; done"
            socket=`eval "$cmd"`
            echo $socket
        done
