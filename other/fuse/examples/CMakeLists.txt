function(add_example NAME SOURCE DEFS)
  add_executable(${NAME} ${SOURCE})
  target_compile_definitions(${NAME} PRIVATE ${DEFS})
  target_link_libraries(${NAME} PRIVATE fuse)
endfunction()

# using fuse interface
add_example(move move.cpp "")
add_example(list_test list_test.cpp "")

# compile for all the different STM types using verlib inteface
add_example(move_notx move_verlib.cpp "")
add_example(move_notx_versioned move_verlib.cpp "Versioned")
add_example(move_mv_stm move_verlib.cpp "MV_STM")
add_example(move_mv_tlf move_verlib.cpp "MV_TLF")
add_example(move_mv_tlf_stm move_verlib.cpp "MV_TLF_STM")
add_example(move_2plsf_stm move_verlib.cpp "SV_STM;TWOPLSF")
add_example(move_2plsf_tlf move_verlib.cpp "SV_TLF;TWOPLSF")
add_example(move_2plsf_tlf_stm move_verlib.cpp "SV_TLF_STM;TWOPLSF")

