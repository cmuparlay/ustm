#!/bin/bash

hasRTM=`hostname`
if [ "$hasRTM" == "pyke" ]
then
      echo 1
else
      echo 0
fi
