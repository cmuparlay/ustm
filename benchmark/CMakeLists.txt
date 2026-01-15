set(MIXED mixed.cpp)
set(TPCC tpcc.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)
set(FUSE_DIR ${OTHER_DIR}/fuse)

set(STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "list" "skiplist")

set(COMMON_DEFS "STMStats")

function(add_benchmark NAME SOURCE STRUCT_NAME DEFS LIB)
  add_executable(${NAME} ${SOURCE})
  target_compile_definitions(${NAME} PRIVATE ${COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE ustm ${LIB})
  
  # Fuse include path must come first to use fuse's parlay
  if("${DEFS}" MATCHES "Fuse")
    target_include_directories(${NAME} PRIVATE ${FUSE_DIR}/include)
  endif()
  
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
  target_include_directories(${NAME} PRIVATE ${PROJECT_SOURCE_DIR}/benchmark)
  target_include_directories(${NAME} PRIVATE ${OTHER_DIR})
  
  # Special handling for HyTm benchmarks
  if("${DEFS}" MATCHES "HyTm")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/phytmlib)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/tmlib)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench)
    # HyTm requires numa library
    target_link_libraries(${NAME} PRIVATE numa)
  endif()
endfunction()

foreach(bench ${STRUCTS})
  add_benchmark(${bench}_ustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_alloc ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_lzstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LazyStamp;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp;ParlayAlloc" "")
  add_benchmark(${bench}_2plsf ${MIXED} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
  add_benchmark(${bench}_hytm ${MIXED} ${STRUCT_DIR}/${bench} "Other;HyTm" "")
  add_benchmark(${bench}_hytm_htm ${MIXED} ${STRUCT_DIR}/${bench} "Other;HyTm;HyTmHTM" "")
  add_benchmark(${bench}_tiny ${MIXED} ${STRUCT_DIR}/${bench} "Other;Tiny" "${PROJECT_SOURCE_DIR}/other/tinystm/lib/libstm.a")
  add_benchmark(${bench}_tl2 ${MIXED} ${STRUCT_DIR}/${bench} "Other;TL2O" "${PROJECT_SOURCE_DIR}/other/tl2-x86/libtl2.a")
  add_benchmark(${bench}_onefile ${MIXED} ${STRUCT_DIR}/${bench} "Other;OneFile" "")
  add_benchmark(${bench}_fuse ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;HWWriteStamp" "")
endforeach()

set(TPCC_STRUCTS "btree" "hash_block" "arttree" "avltree")

foreach(bench ${TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_mvustm_alloc ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(tpcc_${bench}_ustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_ustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_ustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;HWStamp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_ustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;HWStamp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_mvustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_mvustm_trythrow ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_mvustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_mvustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;LongJmp;ParlayAlloc" "")
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


