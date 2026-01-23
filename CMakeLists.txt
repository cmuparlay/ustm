cmake_minimum_required(VERSION 3.13)
project(USTM VERSION 1.0 DESCRIPTION "Micro Software Transactional Memory" LANGUAGES CXX)

# Set a default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug Release RelWithDebInfo MinSizeRel)" FORCE)
  message(STATUS "No build type specified. Defaulted to RELEASE.")
  message(STATUS "To specify a build type, add -DCMAKE_BUILD_TYPE=<DEBUG/RELEASE/RELWITHDEBINFO/MINSIZEREL>")
endif(NOT CMAKE_BUILD_TYPE)

#Try to find a compatible compiler (g++-13 and up).
#This is only needed for gcc's TM, so it might be overkill.
#We only tested on g++-13, it is assumed that newer versions work.
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  set(_TRY_COMPILERS 20 19 18 17 16 15 14 13)
  set(_FOUND false)
  foreach(vers IN LISTS _TRY_COMPILERS)
    find_program(_FOUND_${vers} NAMES g++-${vers})
    if(_FOUND_${vers})
      set(CMAKE_CXX_COMPILER "${_FOUND_${vers}}" CACHE FILEPATH "C++ compiler" FORCE)
      set(_FOUND true)
      break()
    endif()
  endforeach()
  #did not find compatible compiler
  if(NOT _FOUND)
    message(FATAL_ERROR
      "GCC/g++ >= 13 required; detected ${CMAKE_CXX_COMPILER} version ${CMAKE_CXX_COMPILER_VERSION}.\n"
      "Use a newer compiler (e.g., g++-13) by adding -DCMAKE_CXX_COMPILER=g++-13. ${_FOUND}")
  endif()
endif()

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")


message(STATUS "--------------- General configuration -------------")
message(STATUS "CMake Generator:                ${CMAKE_GENERATOR}")
message(STATUS "Compiler:                       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER}")
message(STATUS "Build type:                     ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_CXX_FLAGS:                ${CMAKE_CXX_FLAGS}")
message(STATUS "CMAKE_CXX_FLAGS_DEBUG:          ${CMAKE_CXX_FLAGS_DEBUG}")
message(STATUS "CMAKE_CXX_FLAGS_RELEASE:        ${CMAKE_CXX_FLAGS_RELEASE}")
message(STATUS "CMAKE_CXX_FLAGS_RELWITHDEBINFO: ${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
message(STATUS "CMAKE_EXE_LINKER_FLAGS          ${CMAKE_EXE_LINKER_FLAGS}")
message(STATUS "CMAKE_INSTALL_PREFIX:           ${CMAKE_INSTALL_PREFIX}" )

# -------------------------------------------------------------------
#                       Library definition

set(USTM_INCLUDE_DIR "${USTM_SOURCE_DIR}/include")

add_library(ustm INTERFACE)
target_include_directories(ustm INTERFACE ${USTM_INCLUDE_DIR})
target_compile_features(ustm INTERFACE cxx_std_17)
target_compile_options(ustm INTERFACE -mcx16 -march=native -g)

# Find threading library
find_package(Threads REQUIRED)
target_link_libraries(ustm INTERFACE Threads::Threads)

# Link against jemalloc
find_library(JEMALLOC_LIB jemalloc)
if(NOT JEMALLOC_LIB)
  message(FATAL_ERROR "Could not find jemalloc.")
else()
  message("-- Found jemalloc:                 ${JEMALLOC_LIB}")
  target_link_libraries(ustm INTERFACE optimized ${JEMALLOC_LIB})
endif()

include(ExternalProject)
#set(LIBITM_PREFIX "${CMAKE_BINARY_DIR}/libitm")
#set(LIBITM_SRC_DIR "${LIBITM_PREFIX}/src/project_libitm")
#set(LIBITM_BUILD_DIR "${LIBITM_PREFIX}/src/project_libitm-build")
#set(LIBITM_LIB "${LIBITM_BUILD_DIR}/.libs/libitm.a")
set(LIBITM_PREFIX "${CMAKE_BINARY_DIR}/libitm_ext")
set(LIBITM_LIB "${LIBITM_PREFIX}/src/project_libitm-build/.libs/libitm.a")
ExternalProject_Add(
  project_libitm
  GIT_REPOSITORY https://github.com/ajccosta/gcc.git
  GIT_TAG gcc-13
  PREFIX ${LIBITM_PREFIX}
  GIT_SHALLOW 1
  CONFIGURE_COMMAND <SOURCE_DIR>/libitm/configure
      --enable-static
      --disable-shared 
      --disable-multilib
      --with-pic
      "CXXFLAGS=-fPIC -g -O2"
      "CFLAGS=-fPIC -g -O2"
  BUILD_COMMAND make -j
  INSTALL_COMMAND ""
  BUILD_IN_SOURCE 0
  BUILD_BYPRODUCTS ${LIBITM_LIB}
  UPDATE_COMMAND ""
)
add_library(libitm_static STATIC IMPORTED GLOBAL)
set_target_properties(libitm_static PROPERTIES IMPORTED_LOCATION ${LIBITM_LIB})
add_dependencies(libitm_static project_libitm)
add_dependencies(ustm libitm_static)


# -------------------------------------------------------------------
#                       Benchmarks

add_subdirectory(benchmark)
add_subdirectory(test)
