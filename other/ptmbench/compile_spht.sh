#!/bin/bash

sphtDir=spht

echo "$sphtDir/deps/htm_alg"
make clean --directory ./$sphtDir/deps/htm_alg
make --directory ./$sphtDir/deps/htm_alg
echo "---------------------------"
echo ""
echo "$sphtDir/nvhtm"
make clean --directory ./$sphtDir/nvhtm
make --directory ./$sphtDir/nvhtm
echo "---------------------------"
echo ""
echo ""
echo ""
make -j new-ds-reclaim-spht partial_algorithm_names="abtree hashmap"  