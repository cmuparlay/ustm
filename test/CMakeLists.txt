set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)

function(add_test NAME SOURCE DEFS LIB)
  add_executable(${NAME} ${SOURCE})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_include_directories(${NAME} PRIVATE ${PROJECT_SOURCE_DIR}/benchmark)
  target_link_libraries(${NAME} PRIVATE ustm ${LIB})
  target_include_directories(${NAME} PRIVATE ${OTHER_DIR})

  if("${DEFS}" MATCHES "MULTIVERSE")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/ptmbench/setbench/common)
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/multiverse)
  endif()

  if("${DEFS}" MATCHES "Fuse")
    target_include_directories(${NAME} PRIVATE ${OTHER_DIR}/fuse/include)
  endif()

endfunction()

set(TESTS "test")

foreach(test ${TESTS})
  add_test(${test}_mvustm ${test}.cpp "USTM;MV;SplitStamp" "")
  add_test(${test}_ustm_simple_longjmp ${test}.cpp "USTM;SplitStamp;SimpleSTM;LongJmp" "")
  add_test(${test}_ustm_longjmp ${test}.cpp "USTM;SplitStamp;LongJmp" "")
  add_test(${test}_mvustm_lzstamp ${test}.cpp "USTM;MV;LazyStamp" "")
  add_test(${test}_mvustm_longjmp ${test}.cpp "USTM;MV;SplitStamp;LongJmp" "")
  add_test(${test}_mvustm_trythrow ${test}.cpp "USTM;MV;SplitStamp;TryThrow" "")
  add_test(${test}_2plsf ${test}.cpp "Other;TWOPLSF" "")
  add_test(${test}_fuse ${test}.cpp "Other;Fuse;WriteStamp;MV_TLF" "")
  add_test(${test}_multiverse ${test}.cpp "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")
endforeach()
