set(SOURCES ../test_sets.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/include/structures)
set(STM_STRUCT_DIR ${PROJECT_SOURCE_DIR}/include/stm_structures)

copy_bench_file("runtrans")

set(FUSE_BENCH "leaftree" "dlist" "btree" "list" "hash_block" "arttree" "skiplist" "avltree" "treap")

set(YCSB_COMMON_DEFS "HWWriteStamp")

function(add_benchmark NAME STRUCT_NAME DEFS)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE fuse)
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

function(add_external_benchmark NAME STRUCT_NAME DEFS LIB)
  add_executable(${NAME} ${SOURCES})
  target_link_options(${NAME} PRIVATE -no-pie)
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE fuse ${LIB})
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

function(add_external_benchmark NAME STRUCT_NAME DEFS LIB)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE fuse ${LIB})
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
endfunction()

foreach(bench ${FUSE_BENCH})
  add_benchmark(${bench} ${STRUCT_DIR}/${bench} "")
  add_benchmark(${bench}_versioned ${STRUCT_DIR}/${bench} "Versioned")
  add_benchmark(${bench}_mv_tlf ${STRUCT_DIR}/${bench} "MV_TLF")
  add_benchmark(${bench}_mv_tlf_stm ${STRUCT_DIR}/${bench} "MV_TLF_STM")
  add_benchmark(${bench}_2plsf_tlf ${STRUCT_DIR}/${bench} "TWOPLSF;SV_TLF" "")
  add_benchmark(${bench}_2plsf_tlf_stm ${STRUCT_DIR}/${bench} "TWOPLSF;SV_TLF_STM" "")
endforeach()

add_benchmark(leaftree_fuse ${STRUCT_DIR}/tlf_leaftree "MV_TLF")

set(STM_BENCH "arttree" "skiplist" "list" "hash_block" "btree" "leaftree" "avltree" "treap")

foreach(bench ${STM_BENCH})
  add_benchmark(${bench}_mv_stm ${STM_STRUCT_DIR}/${bench} "MV_STM")
  add_benchmark(${bench}_2plsf_stm ${STM_STRUCT_DIR}/${bench} "TWOPLSF;SV_STM" "")
#  add_external_benchmark(${bench}_tiny ${STM_STRUCT_DIR}/${bench} "TINYSTM;SV_STM" "${PROJECT_SOURCE_DIR}/include/stms/tinystm/lib/libstm.a")
#  add_external_benchmark(${bench}_tl2 ${STM_STRUCT_DIR}/${bench} "TL2O;SV_STM" "${PROJECT_SOURCE_DIR}/include/stms/tl2-x86/libtl2.a")
endforeach()


