cmake_minimum_required(VERSION 3.13)
project(USTM VERSION 1.0 DESCRIPTION "Micro Software Transactional Memory" LANGUAGES CXX)

# Set a default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug Release RelWithDebInfo MinSizeRel)" FORCE)
  message(STATUS "No build type specified. Defaulted to RELEASE.")
  message(STATUS "To specify a build type, add -DCMAKE_BUILD_TYPE=<DEBUG/RELEASE/RELWITHDEBINFO/MINSIZEREL>")
endif(NOT CMAKE_BUILD_TYPE)

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")

set (ALLOCATOR "deqalloc")
#set (ALLOCATOR "jemalloc")

message(STATUS "--------------- General configuration -------------")
message(STATUS "CMake Generator:                ${CMAKE_GENERATOR}")
message(STATUS "Compiler:                       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Allocator:                      ${ALLOCATOR}" )
message(STATUS "Build type:                     ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS          ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_INSTALL_PREFIX:           ${CMAKE_INSTALL_PREFIX}" )
message(STATUS "CMAKE_INSTALL_PREFIX:           ${CMAKE_INSTALL_PREFIX}" )

# -------------------------------------------------------------------
#                       Library definition

set(USTM_INCLUDE_DIR "${USTM_SOURCE_DIR}/include")

add_library(ustm INTERFACE)
target_include_directories(ustm INTERFACE ${USTM_INCLUDE_DIR})
target_compile_features(ustm INTERFACE cxx_std_17)
target_compile_options(ustm INTERFACE -march=native -g)

# Find threading library
find_package(Threads REQUIRED)
target_link_libraries(ustm INTERFACE Threads::Threads)

if(ALLOCATOR MATCHES "jemalloc")
  # Link against jemalloc
  find_library(JEMALLOC_LIB jemalloc)
  if(NOT JEMALLOC_LIB)
    message(FATAL_ERROR "Could not find jemalloc.")
  else()
    message("-- Found jemalloc: ${JEMALLOC_LIB}")
    set(COMMON_DEFS JEMALLOC)
    set(COMMON_LINKS ${JEMALLOC_LIB})
    target_link_libraries(ustm INTERFACE optimized ${JEMALLOC_LIB})
  endif()

elseif(ALLOCATOR MATCHES "deqalloc")
  message(WARNING "deqalloc will be compiled but you must still link it with LD_PRELOAD.")
  include(FetchContent)
  #fetch deqalloc from github
  FetchContent_Declare(
    deqalloc
    GIT_REPOSITORY https://github.com/ajccosta/deqalloc
    GIT_TAG        91d9d20
    SOURCE_SUBDIR examples/deqalloc
  )
  FetchContent_MakeAvailable(deqalloc)
  #add target to force deqalloc to be build
  add_custom_target(force_build_deqalloc ALL)
  add_dependencies(force_build_deqalloc deqalloc)
  #add target to force symlink creation
  add_custom_command(
      TARGET force_build_deqalloc
      COMMAND ${CMAKE_COMMAND} -E create_symlink
          "$<TARGET_FILE:deqalloc>"
          "${CMAKE_BINARY_DIR}/libdeqalloc.so"
      BYPRODUCTS "${CMAKE_BINARY_DIR}/libdeqalloc.so"
      COMMENT "Symlink libdeqalloc.so to root build directory"
  )
else()
  message(FATAL_ERROR "No allocator selected. Allocator options: deqalloc, jemalloc.")
endif()
# -------------------------------------------------------------------
#                       Benchmarks

add_subdirectory(benchmark)
add_subdirectory(test)
