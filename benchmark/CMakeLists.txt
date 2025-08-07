set(SOURCES mixed_operation_benchmark.cpp)
set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)

set(STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "list" "skiplist")

set(YCSB_COMMON_DEFS "STMStats")

function(add_benchmark NAME STRUCT_NAME DEFS LIB)
  add_executable(${NAME} ${SOURCES})
  target_compile_definitions(${NAME} PRIVATE ${YCSB_COMMON_DEFS})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE ustm ${LIB})
  target_include_directories(${NAME} PRIVATE ${STRUCT_NAME})
  target_include_directories(${NAME} PRIVATE ${PROJECT_SOURCE_DIR}/benchmark)
  target_include_directories(${NAME} PRIVATE ${OTHER_DIR})
endfunction()

foreach(bench ${STRUCTS})
  add_benchmark(${bench}_ustm_longjmp ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp" "")
  add_benchmark(${bench}_mvustm ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_hwstamp ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
  add_benchmark(${bench}_mvustm_hwstamp_unprotected ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
  add_benchmark(${bench}_2plsf ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
  add_benchmark(${bench}_tiny ${STRUCT_DIR}/${bench} "Other;Tiny" "${PROJECT_SOURCE_DIR}/other/tinystm/lib/libstm.a")
  #  add_benchmark(${bench}_tl2 ${STRUCT_DIR}/${bench} "Other;TL2O" "${PROJECT_SOURCE_DIR}/other/tl2-x86/libtl2.a")
  add_benchmark(${bench}_onefile ${STRUCT_DIR}/${bench} "Other;OneFile" "")
endforeach()

set(NSTRUCTS "hash_probe")

foreach(bench ${NSTRUCTS})
  add_benchmark(${bench}_mvustm ${STRUCT_DIR}/${bench} "USTM;MV" "")
  add_benchmark(${bench}_mvustm_hwstamp ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow" "")
  add_benchmark(${bench}_mvustm_unprotected ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
  add_benchmark(${bench}_mvustm_hwstamp_unprotected ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
endforeach()


