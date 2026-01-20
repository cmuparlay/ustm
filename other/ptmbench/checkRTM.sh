#!/bin/bash

hasRTM=`lscpu | grep rtm`
if [ -z "$hasRTM" ]
then
      echo "0"
else
      echo "1"
fi
