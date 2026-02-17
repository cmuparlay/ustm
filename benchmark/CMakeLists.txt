set(MIXED mixed.cpp)
set(TPCC tpcc.cpp)

# Link against jemalloc
find_library(JEMALLOC_LIB jemalloc)
if(NOT JEMALLOC_LIB)
  message(FATAL_ERROR "Could not find jemalloc.")
else()
  message("-- Found jemalloc: ${JEMALLOC_LIB}")
  set(COMMON_DEFS JEMALLOC)
  set(COMMON_LINKS ${JEMALLOC_LIB})
endif()

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)
set(FUSE_DIR ${OTHER_DIR}/fuse)

set(STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "hash_list" "list" "skiplist")

list(APPEND COMMON_DEFS "STMStats")

function(add_benchmark NAME SOURCE STRUCT_NAME DEFS LIB)
  #only build hwstamp versions on x86
  if((NOT "${TARGET_ARCH}" MATCHES "x86"))
    #dont build ustm with hwstamp
    if("${DEFS}" MATCHES "HWStamp" )
      return()
    endif()
    #fuse will build but wont use hwstamps
    if("${DEFS}" MATCHES "HWWriteStamp")
      list(REMOVE_ITEM DEFS HWWriteStamp)
    endif()
  endif()

  add_executable(${NAME} ${SOURCE})
  target_compile_definitions(${NAME} PRIVATE ${DEFS} ${COMMON_DEFS})
  target_link_libraries(${NAME} PRIVATE ustm ${LIB} ${COMMON_LINKS})

  target_compile_definitions(${NAME} PRIVATE JEMALLOC)
  
  # Fuse include path must come first to use fuse's parlay
  if("${DEFS}" MATCHES "Fuse")
    target_include_directories(${NAME} PRIVATE ${FUSE_DIR}/include)
  endif()
  
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
  target_include_directories(${NAME} PRIVATE ${PROJECT_SOURCE_DIR}/benchmark)
  target_include_directories(${NAME} PRIVATE ${OTHER_DIR})
  
  if("${DEFS}" MATCHES "HyTm")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/phytmlib)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/tmlib)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench)
    target_link_libraries(${NAME} PRIVATE numa)
  endif()

  if("${DEFS}" MATCHES "MULTIVERSE")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/setbench/common)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/multiverse)
  endif()

  if("${DEFS}" MATCHES "dcotl")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/setbench/common)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/multiverse)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/multiverse/tm_common)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/dcotl)
  endif()
endfunction()

foreach(bench ${STRUCTS})
  add_benchmark(${bench}_ustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp" "")
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_alloc ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_lzstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LazyStamp" "")
  add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")

  # Use agg funnels for lazy timestamp
  add_benchmark(${bench}_mvustm_agg_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;SimpleFunnel" "")
  add_benchmark(${bench}_mvustm_agg_funnel_uepoch ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;SimpleFunnel;USE_UEPOCH" "")
  add_benchmark(${bench}_mvustm_full_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;FullFunnel" "")
  add_benchmark(${bench}_mvustm_recursive_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;RecursiveFunnel" "")

  # Use agg funnels for eager timestamp

  add_benchmark(${bench}_mvustm_agg_funnel_eager_uepoch ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;SimpleFunnel;USE_UEPOCH" "")
  add_benchmark(${bench}_mvustm_agg_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;SimpleFunnel" "")
  add_benchmark(${bench}_mvustm_full_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;FullFunnel" "")
  add_benchmark(${bench}_mvustm_recursive_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;RecursiveFunnel" "")

  add_benchmark(${bench}_mvustm_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Eager" "")

  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
  add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
  add_benchmark(${bench}_2plsf ${MIXED} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
  add_benchmark(${bench}_hytm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm" "")
  add_benchmark(${bench}_hytm_htm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm;HyTmHTM" "")
  add_benchmark(${bench}_multiverse ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")
  add_benchmark(${bench}_dcotl ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MICROBENCH;dcotl" "")
  add_benchmark(${bench}_tiny ${MIXED} ${STRUCT_DIR}/${bench} "Other;Tiny" tinystm)
  add_benchmark(${bench}_tl2 ${MIXED} ${STRUCT_DIR}/${bench} "Other;TL2O" "${PROJECT_SOURCE_DIR}/other/tl2-x86/libtl2.a")
  add_benchmark(${bench}_onefile ${MIXED} ${STRUCT_DIR}/${bench} "Other;OneFile" "")
  add_benchmark(${bench}_fuse ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;HWWriteStamp;MV_TLF" "")
endforeach()

set(TPCC_STRUCTS "btree" "hash_block" "arttree" "avltree")

foreach(bench ${TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(tpcc_${bench}_mvustm_alloc ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_ustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM" "")
  add_benchmark(tpcc_${bench}_ustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp" "")
  add_benchmark(tpcc_${bench}_ustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;HWStamp" "")
  add_benchmark(tpcc_${bench}_ustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;HWStamp" "")
  add_benchmark(tpcc_${bench}_mvustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(tpcc_${bench}_mvustm_trythrow ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(tpcc_${bench}_mvustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
  add_benchmark(tpcc_${bench}_mvustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;LongJmp" "")
  add_benchmark(tpcc_${bench}_2plsf ${TPCC} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
  add_benchmark(tpcc_${bench}_hytm ${TPCC} ${STRUCT_DIR}/${bench} "Other;HyTm" "")
endforeach()

add_benchmark(tpcc_mix_2plsf ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;TWOPLSF;Mix" "")
add_benchmark(tpcc_mix_hytm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;HyTm;Mix" "")
add_benchmark(tpcc_mix_mvustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;MV;Mix" "")
add_benchmark(tpcc_mix_ustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;SimpleSTM;Mix" "")
add_benchmark(tpcc_mix_ustm_longjmp_hwstamp ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;SimpleSTM;Mix;LongJmp;HWStamp" "")

add_benchmark(tpcc_hash_probe_mvustm ${TPCC} ${STRUCT_DIR}/hash_probe "USTM;MV" "")
  
set(NSTRUCTS "hash_probe")

foreach(bench ${NSTRUCTS})
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
  add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
endforeach()


