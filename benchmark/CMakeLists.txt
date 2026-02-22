set(MIXED mixed.cpp)
set(TPCC tpcc.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)
set(FUSE_DIR ${OTHER_DIR}/fuse)

set(STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "hash_list" "list" "skiplist" "hash_probe")
set(SUB_STRUCTS "btree")

set(COMMON_DEFS "STMStats")

function(add_benchmark NAME SOURCE STRUCT_NAME DEFS LIB)
  add_executable(${NAME} ${SOURCE})
  target_compile_definitions(${NAME} PRIVATE ${DEFS} ${COMMON_DEFS})
  target_link_libraries(${NAME} PRIVATE ustm ${LIB} ${COMMON_LINKS})

  if(ALLOCATOR MATCHES "jemalloc")
    target_compile_definitions(${NAME} PRIVATE JEMALLOC)
  else() #deqalloc
    add_dependencies(${NAME} force_build_deqalloc)
  endif()

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

add_benchmark(btree_inplace_mvustm ${MIXED} ${STRUCT_DIR}/btree "USTM;MV;UpsertInPlace" "")
add_benchmark(btree_inplace_2plsf ${MIXED} ${STRUCT_DIR}/btree "Other;TWOPLSF;UpsertInPlace" "")
add_benchmark(btree_inplace_fuse ${MIXED} ${STRUCT_DIR}/btree "Other;Fuse;WriteStamp;MV_TLF;UpsertInPlace" "")
add_benchmark(btree_inplace_multiverse ${MIXED} ${STRUCT_DIR}/btree "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;UpsertInPlace" "")

foreach(bench ${SUB_STRUCTS})
  add_benchmark(${bench}_ustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM" "")
  add_benchmark(${bench}_ustm_simple_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp" "")
  add_benchmark(${bench}_ustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;LongJmp" "")
  add_benchmark(${bench}_mvustm_alloc ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_lzstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LazyStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")

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
endforeach()

foreach(bench ${STRUCTS})
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_vla ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;VLA" "")
  add_benchmark(${bench}_2plsf ${MIXED} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")

  add_benchmark(${bench}_multiverse ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")

  if (NOT ${bench} MATCHES "hash_probe|hash_list|hash_block")
    add_benchmark(${bench}_dcotl ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MICROBENCH;dcotl" "")
  endif()

#  add_benchmark(${bench}_tl2 ${MIXED} ${STRUCT_DIR}/${bench} "Other;TL2O" "${PROJECT_SOURCE_DIR}/other/tl2-x86/libtl2.a")
#  add_benchmark(${bench}_onefile ${MIXED} ${STRUCT_DIR}/${bench} "Other;OneFile" "")
  add_benchmark(${bench}_fuse ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;WriteStamp;MV_TLF" "")
endforeach()

set(TPCC_STRUCTS "btree" "hash_block" "arttree" "avltree")
set(SUB_TPCC_STRUCTS "btree")

foreach(bench ${SUB_TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(tpcc_${bench}_2plsf ${TPCC} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
endforeach()
  
foreach(bench ${TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm_alloc ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_ustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM" "")
  add_benchmark(tpcc_${bench}_ustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp" "")
  add_benchmark(tpcc_${bench}_mvustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(tpcc_${bench}_mvustm_trythrow ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(tpcc_${bench}_multiverse ${TPCC} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")
  add_benchmark(tpcc_${bench}_fuse ${TPCC} ${STRUCT_DIR}/${bench} "Other;Fuse;WriteStamp;MV_TLF" "")
endforeach()

add_benchmark(tpcc_mix_2plsf ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;TWOPLSF;Mix" "")
add_benchmark(tpcc_mix_mvustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;MV;Mix" "")
add_benchmark(tpcc_mix_ustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;SimpleSTM;Mix" "")
add_benchmark(tpcc_mix_multiverse ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;Mix" "")
add_benchmark(tpcc_mix_fuse ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;Fuse;WriteStamp;MV_TLF;Mix" "")

add_benchmark(tpcc_hash_probe_mvustm ${TPCC} ${STRUCT_DIR}/hash_probe "USTM;MV" "")
  
set(NSTRUCTS "")

foreach(bench ${NSTRUCTS})
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
endforeach()

#x86-only benchmarks
if (TARGET_ARCH MATCHES "x86")
  foreach(bench ${SUB_STRUCTS})
    add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")  # Use agg funnels for lazy timestamp
    add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
    add_benchmark(${bench}_fuse_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;HWWriteStamp;MV_TLF" "")
    add_benchmark(${bench}_multiverse_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;USE_RDTSC" "")
  endforeach()

  foreach(bench ${STRUCTS})
    if (NOT ${bench} MATCHES "hash_probe|hash_list|hash_block" AND NOT TARGET_ARCH MATCHES "arm")
      add_benchmark(${bench}_hytm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm" "")
      add_benchmark(${bench}_hytm_htm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm;HyTmHTM" "")
    endif()
    add_benchmark(${bench}_tiny ${MIXED} ${STRUCT_DIR}/${bench} "Other;Tiny" "${PROJECT_SOURCE_DIR}/other/tinystm/lib/libstm.a")
  endforeach()

  foreach(bench ${TPCC_STRUCTS})
    add_benchmark(tpcc_${bench}_ustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;HWStamp" "")
    add_benchmark(tpcc_${bench}_ustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;HWStamp" "")
    add_benchmark(tpcc_${bench}_mvustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
    add_benchmark(tpcc_${bench}_mvustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;LongJmp" "")
  endforeach()

  add_benchmark(tpcc_mix_ustm_longjmp_hwstamp ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;SimpleSTM;Mix;LongJmp;HWStamp" "")

  foreach(bench ${NSTRUCTS})
    add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
    add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
  endforeach()
endif()

#benchmarks incompatible with arm
if (NOT TARGET_ARCH MATCHES "arm")
  add_benchmark(tpcc_mix_hytm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;HyTm;Mix" "")

  foreach(bench ${SUB_TPCC_STRUCTS})
    add_benchmark(tpcc_${bench}_hytm ${TPCC} ${STRUCT_DIR}/${bench} "Other;HyTm" "")
  endforeach()
endif()