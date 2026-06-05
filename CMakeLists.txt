cmake_minimum_required(VERSION 3.13)
project(USTM VERSION 1.0 DESCRIPTION "Micro Software Transactional Memory" LANGUAGES C CXX)

# Set a default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type (Debug Release RelWithDebInfo MinSizeRel)" FORCE)
  message(STATUS "No build type specified. Defaulted to RELEASE.")
  message(STATUS "To specify a build type, add -DCMAKE_BUILD_TYPE=<DEBUG/RELEASE/RELWITHDEBINFO/MINSIZEREL>")
endif(NOT CMAKE_BUILD_TYPE)

set (CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -O0 -fno-omit-frame-pointer")

#stop warnings for benign memcpy
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wstringop-overflow=0")
#silence warning "parameter passing for argument of type ‘std::pair<double, double>’ when C++17 is enabled changed to match C++14 in GCC 10.1"
set (CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-psabi")

#lower case arch label
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" ARCH)
#detect architecture

if(ARCH MATCHES "^(x86|x86_64|amd64|i[3-6]86)$")
  set(TARGET_ARCH "x86")
  list(APPEND COMMON_DEFS "USTM_X86")
elseif(ARCH MATCHES "^(aarch64|arm64)$")
  set(TARGET_ARCH "arm")
  list(APPEND COMMON_DEFS "USTM_ARM")
elseif(ARCH MATCHES "^(ppc64le|ppc64|powerpc64)$")
  set(TARGET_ARCH "ppc")
  list(APPEND COMMON_DEFS "USTM_PPC")
elseif(ARCH MATCHES "^(arm|ppc|powerpc)$")
  message(FATAL_ERROR "Unsupported architecture: ${ARCH}.")
else()
  message(FATAL_ERROR "Unknown architecture.")
endif()

set (ALLOCATOR "deqalloc")
#set (ALLOCATOR "jemalloc")

message(STATUS "--------------- General configuration -------------")
message(STATUS "CMake Generator:                ${CMAKE_GENERATOR}")
message(STATUS "Compiler:                       ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Architecture:                   ${TARGET_ARCH}")
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

set(USTM_COMMON_FLAGS -g)
if(TARGET_ARCH MATCHES "x86")
  list(APPEND USTM_COMMON_FLAGS -mcx16 -march=native)
elseif(TARGET_ARCH MATCHES "ppc")
  list(APPEND USTM_COMMON_FLAGS -mcpu=native)
else() #arm
  list(APPEND USTM_COMMON_FLAGS -march=native -latomic)
endif()
target_compile_options(ustm INTERFACE ${USTM_COMMON_FLAGS})

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
    list(APPEND COMMON_DEFS "JEMALLOC")
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
    GIT_TAG        d4840fd
  )
  FetchContent_GetProperties(deqalloc)
  if(NOT deqalloc_POPULATED)
    FetchContent_Populate(deqalloc)
    add_subdirectory(${deqalloc_SOURCE_DIR}/examples/deqalloc ${deqalloc_BINARY_DIR})
  endif()
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

#compile tiny
include(ExternalProject)
string(REPLACE ";" " " USTM_COMMON_CFLAGS_STR "${USTM_COMMON_FLAGS}")
ExternalProject_Add(tinystm_ext
    SOURCE_DIR ${PROJECT_SOURCE_DIR}/other/tinystm
    BUILD_IN_SOURCE 1
    CONFIGURE_COMMAND ""
    BUILD_COMMAND $(MAKE)
      CFLAGS=${USTM_COMMON_CFLAGS_STR}
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS ${PROJECT_SOURCE_DIR}/other/tinystm/lib/libstm.a
)
add_library(tinystm STATIC IMPORTED)
set_target_properties(tinystm PROPERTIES
    IMPORTED_LOCATION ${PROJECT_SOURCE_DIR}/other/tinystm/lib/libstm.a
)
add_dependencies(tinystm tinystm_ext)

# -------------------------------------------------------------------
#                       Benchmarks

add_subdirectory(benchmark)
add_subdirectory(test)
