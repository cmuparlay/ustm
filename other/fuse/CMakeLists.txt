cmake_minimum_required(VERSION 3.13)
project(FUSE VERSION 1.0 DESCRIPTION "Transactional Lock Fusion" LANGUAGES CXX)

# Set a default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug Release RelWithDebInfo MinSizeRel)" FORCE)
  message(STATUS "No build type specified. Defaulted to RELEASE.")
  message(STATUS "To specify a build type, add -DCMAKE_BUILD_TYPE=<DEBUG/RELEASE/RELWITHDEBINFO/MINSIZEREL>")
endif(NOT CMAKE_BUILD_TYPE)

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")
# set (CMAKE_LINKER_FLAGS_DEBUG "${CMAKE_LINKER_FLAGS_DEBUG} -no-pie -O0 -fno-omit-frame-pointer")

message(STATUS "--------------- General configuration -------------")
message(STATUS "CMake Generator:                ${CMAKE_GENERATOR}")
message(STATUS "Compiler:                       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build type:                     ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS          ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_INSTALL_PREFIX:           ${CMAKE_INSTALL_PREFIX}" )

# -------------------------------------------------------------------
#                       Library definition

set(FUSE_INCLUDE_DIR "${FUSE_SOURCE_DIR}/include")

add_library(fuse INTERFACE)
target_include_directories(fuse INTERFACE ${FUSE_INCLUDE_DIR})
target_compile_features(fuse INTERFACE cxx_std_17)
target_compile_options(fuse INTERFACE -mcx16 -march=native -g)

add_library(verlib INTERFACE)
target_include_directories(verlib INTERFACE ${FUSE_INCLUDE_DIR})
target_compile_features(verlib INTERFACE cxx_std_17)
target_compile_options(verlib INTERFACE -mcx16 -march=native -g)

# Find threading library
find_package(Threads REQUIRED)
target_link_libraries(fuse INTERFACE Threads::Threads)

# Link against jemalloc
find_library(JEMALLOC_LIB jemalloc)
if(NOT JEMALLOC_LIB)
  message(FATAL_ERROR "Could not find jemalloc.")
else()
  message("-- Found jemalloc: ${JEMALLOC_LIB}")
  target_link_libraries(fuse INTERFACE optimized ${JEMALLOC_LIB})
endif()

# -------------------------------------------------------------------
#                       Benchmarks

add_subdirectory(benchmark)
add_subdirectory(examples)
