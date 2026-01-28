#!/bin/bash

set -e -x -o pipefail

# make -j ds_no_lib_lambda_framework NO_LIB_TM_ALGS="dcotl" partial_algorithm_names="hashmap"
# make -j ds_no_lib_lambda_framework NO_LIB_TM_ALGS="multiverse" partial_algorithm_names="hashmap"
# make -j tmlib partial_algorithm_names="hashmap"
# make -j ds_tinystm partial_algorithm_names="hashmap"

# make -j ds_no_lib_lambda_framework NO_LIB_TM_ALGS="dcotl" partial_algorithm_names="abtree brown_"
# make -j ds_no_lib_lambda_framework NO_LIB_TM_ALGS="multiverse" partial_algorithm_names="abtree brown_"
# make -j tmlib partial_algorithm_names="abtree brown_"
# make -j ds_tinystm partial_algorithm_names="abtree brown_"

# make -j ds_no_lib_lambda_framework partial_algorithm_names="abtree brown_"

make -j all

