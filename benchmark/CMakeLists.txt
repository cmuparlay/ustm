set(MIXED mixed.cpp)
set(TPCC tpcc.cpp)

set(STRUCT_DIR ${PROJECT_SOURCE_DIR}/structures)
set(OL_STRUCT_DIR ${PROJECT_SOURCE_DIR}/other/fuse/include/structures)
set(OTHER_DIR ${PROJECT_SOURCE_DIR}/other)
set(FUSE_DIR ${OTHER_DIR}/fuse)

set(STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "hash_list" "list" "skiplist" "hash_probe")
set(SUB_STRUCTS "btree")
# Structures available as native blocking optimistic-locking implementations in fuse/include/structures.
set(OL_STRUCTS "avltree" "treap" "leaftree" "btree" "arttree" "hash_block" "list" "skiplist")

list(APPEND COMMON_DEFS "STMStats")

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
  if("${DEFS}" MATCHES "Fuse|OL")
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

add_benchmark(hash_probe_string_mvustm ${MIXED} ${STRUCT_DIR}/hash_probe "USTM;MV;SplitStamp;StringValues" "")

add_benchmark(btree_string_mvustm ${MIXED} ${STRUCT_DIR}/btree "USTM;MV;SplitStamp;StringValues" "")
add_benchmark(btree_string_inplace_indirect_mvustm ${MIXED} ${STRUCT_DIR}/btree "USTM;MV;SplitStamp;UpsertInPlace;ForceIndirect;StringValues" "")
add_benchmark(btree_string_inplace_mvustm ${MIXED} ${STRUCT_DIR}/btree "USTM;MV;SplitStamp;UpsertInPlace;StringValues" "")
add_benchmark(btree_string_indirect_mvustm ${MIXED} ${STRUCT_DIR}/btree "USTM;MV;SplitStamp;ForceIndirect;StringValues" "")



add_benchmark(btree_string_2plsf ${MIXED} ${STRUCT_DIR}/btree "Other;TWOPLSF;StringValues" "")
add_benchmark(btree_string_inplace_indirect_2plsf ${MIXED} ${STRUCT_DIR}/btree "Other;TWOPLSF;UpsertInPlace;ForceIndirect;StringValues" "")

add_benchmark(btree_string_fuse ${MIXED} ${STRUCT_DIR}/btree "Other;Fuse;WriteStamp;MV_TLF;StringValues" "")

add_benchmark(btree_string_multiverse ${MIXED} ${STRUCT_DIR}/btree "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;StringValues" "")
add_benchmark(btree_string_inplace_indirect_multiverse ${MIXED} ${STRUCT_DIR}/btree "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;UpsertInPlace;ForceIndirect;StringValues" "")

foreach(bench ${SUB_STRUCTS})
#  add_benchmark(${bench}_ustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SplitStamp;SimpleSTM" "")
  add_benchmark(${bench}_ustm_simple_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SplitStamp;SimpleSTM;LongJmp" "")
  add_benchmark(${bench}_ustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SplitStamp;LongJmp" "")
  add_benchmark(${bench}_mvustm_alloc ${MIXED} ${STRUCT_DIR}/${bench} "USTM;SplitStamp;MV;ParlayAlloc" "")
  add_benchmark(${bench}_mvustm_lzstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;LazyStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;LongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;TryThrow" "")
#  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;UnprotectedLoad" "")

  add_benchmark(${bench}_mvustm_agg_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;SimpleFunnel" "")
  add_benchmark(${bench}_mvustm_agg_funnel_uepoch ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;SimpleFunnel;USE_UEPOCH" "")
  add_benchmark(${bench}_mvustm_full_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Funnel;FullFunnel" "")
#  add_benchmark(${bench}_mvustm_recursive_funnel ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;RecursiveFunnel" "")

  # Use agg funnels for eager timestamp
  add_benchmark(${bench}_mvustm_agg_funnel_eager_uepoch ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;SimpleFunnel;USE_UEPOCH" "")
  add_benchmark(${bench}_mvustm_agg_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;SimpleFunnel" "")
  add_benchmark(${bench}_mvustm_full_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;FullFunnel" "")
  add_benchmark(${bench}_mvustm_recursive_funnel_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;FunnelEager;RecursiveFunnel" "")

  add_benchmark(${bench}_mvustm_eager ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;Eager" "")
endforeach()

foreach(bench ${STRUCTS})
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp" "")
  add_benchmark(${bench}_mvustm_vla ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;VLA" "")
  add_benchmark(${bench}_2plsf ${MIXED} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")

  add_benchmark(${bench}_multiverse ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")

  if (NOT ${bench} MATCHES "hash_probe|hash_list|hash_block")
    add_benchmark(${bench}_dcotl ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MICROBENCH;dcotl" "")
  endif()

#  add_benchmark(${bench}_tl2 ${MIXED} ${STRUCT_DIR}/${bench} "Other;TL2O" "${PROJECT_SOURCE_DIR}/other/tl2-x86/libtl2.a")
  add_benchmark(${bench}_onefile ${MIXED} ${STRUCT_DIR}/${bench} "Other;OneFile" "")
  add_benchmark(${bench}_fuse ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;WriteStamp;MV_TLF" "")
endforeach()

add_benchmark(hash_probe_mvustm_indirect ${MIXED} ${STRUCT_DIR}/hash_probe "USTM;MV;SplitStamp;USTM_INDIRECT" "")

set(TPCC_STRUCTS "btree" "hash_block" "arttree" "avltree")
set(SUB_TPCC_STRUCTS "btree")

foreach(bench ${SUB_TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;ParlayAlloc" "")
  add_benchmark(tpcc_${bench}_2plsf ${TPCC} ${STRUCT_DIR}/${bench} "Other;TWOPLSF" "")
  add_benchmark(tpcc_${bench}_fuse ${TPCC} ${STRUCT_DIR}/${bench} "Other;Fuse;WriteStamp;ParlayAlloc;MV_TLF" "")
endforeach()
  
foreach(bench ${TPCC_STRUCTS})
  add_benchmark(tpcc_${bench}_mvustm_alloc ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp" "")
  # add_benchmark(tpcc_${bench}_ustm ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;ParlayAlloc" "")
  # add_benchmark(tpcc_${bench}_ustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;ParlayAlloc" "")
  # add_benchmark(tpcc_${bench}_mvustm_longjmp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;LongJmp;ParlayAlloc" "")
  # add_benchmark(tpcc_${bench}_mvustm_trythrow ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;TryThrow;ParlayAlloc" "")
  # add_benchmark(tpcc_${bench}_multiverse ${TPCC} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE" "")
  # if (CMAKE_SYSTEM_PROCESSOR MATCHES ${X86})
  #   add_benchmark(tpcc_${bench}_ustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;HWStamp" "")
  #   add_benchmark(tpcc_${bench}_ustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;SimpleSTM;LongJmp;HWStamp" "")
  #   add_benchmark(tpcc_${bench}_mvustm_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
  #   add_benchmark(tpcc_${bench}_mvustm_longjmp_hwstamp ${TPCC} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp;LongJmp" "")
  # endif()
endforeach()

add_benchmark(tpcc_mix_2plsf ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;TWOPLSF;Mix" "")
add_benchmark(tpcc_mix_mvustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;MV;SplitStamp;Mix;ParlayAlloc" "")
add_benchmark(tpcc_mix_ustm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "USTM;SimpleSTM;SplitStamp;Mix;ParlayAlloc" "")
# add_benchmark(tpcc_mix_multiverse ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;Mix" "")
add_benchmark(tpcc_mix_fuse ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;Fuse;WriteStamp;MV_TLF;Mix" "")

add_benchmark(tpcc_hash_probe_mvustm ${TPCC} ${STRUCT_DIR}/hash_probe "USTM;MV;SplitStamp" "")
  
set(NSTRUCTS "")

foreach(bench ${NSTRUCTS})
  add_benchmark(${bench}_mvustm ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStamp" "")
  add_benchmark(${bench}_mvustm_longjmp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStampLongJmp" "")
  add_benchmark(${bench}_mvustm_trythrow ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;SplitStampLongJmp;TryThrow" "")
#  add_benchmark(${bench}_mvustm_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad" "")
endforeach()

#x86-only benchmarks
if (TARGET_ARCH MATCHES "x86")
  # _mvustm_hwstamp_unprotected stays scoped to SUB_STRUCTS — it's an
  # experimental variant intended for one structure at a time. Plain
  # _mvustm_hwstamp / _fuse_hwstamp baselines are emitted by the
  # STRUCTS loop below.
  foreach(bench ${SUB_STRUCTS})
    add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
#    add_benchmark(${bench}_multiverse_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;USE_RDTSC" "")
  endforeach()

  # HWStamp baselines used by run_stm_vs_ol — must cover every structure
  # in the sweep, otherwise _binary_exists() filters the impl out and the
  # bars go missing.
  foreach(bench ${STRUCTS})
    add_benchmark(${bench}_mvustm_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;HWStamp" "")
    add_benchmark(${bench}_fuse_hwstamp ${MIXED} ${STRUCT_DIR}/${bench} "Other;Fuse;HWWriteStamp;MV_TLF" "")
  endforeach()

  foreach(bench ${STRUCTS})
    if (NOT ${bench} MATCHES "hash_probe|hash_list|hash_block" AND NOT TARGET_ARCH MATCHES "arm")
      add_benchmark(${bench}_hytm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm" "")
      add_benchmark(${bench}_hytm_htm ${MIXED} ${STRUCT_DIR}/${bench} "HyTm;HyTmHTM" "")
    endif()
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
#    add_benchmark(${bench}_mvustm_hwstamp_unprotected ${MIXED} ${STRUCT_DIR}/${bench} "USTM;MV;UnprotectedLoad;HWStamp" "")
  endforeach()
endif()

# Blocking optimistic-locking benchmarks. Point-update YCSB-style
# experiments using the plain OL data structures in
# other/fuse/include/structures, with no verlib features (no Versioned,
# no FlockHelp, no LazyStamp): just the bare blocking OL structure with
# flock locks. Uses mixed.cpp directly via the OL glue, so the benchmark
# loop, key generation, and output are identical to the STM impls — only
# the stm::atomic_region wrappers change.
#
# OL_USE_STM routes the structures' versioned_ptr/atomic_bool through
# stm::atomic; for the OL-only build that's just a typedef alias to
# flck::atomic (zero overhead), but it lets the fuse-wrapped _ol_txn
# variants below substitute fuse::tlf_atomic so the structures'
# loads/stores participate in fuse transactions.
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_ol ${MIXED} ${OL_STRUCT_DIR}/${bench} "OL;OL_USE_STM" "")
endforeach()

# Cross-validation against the upstream flock benchmark
# (other/fuse/benchmark/test_sets.cpp). Same plain blocking OL structures
# as ${bench}_ol, but driven by flock's own test harness — used to
# confirm that mixed_ol.cpp's numbers agree with the published benchmark.
# Empty defines reproduce the _lck_noversion configuration.
set(FLOCK_BENCH_SRC ${FUSE_DIR}/benchmark/test_sets.cpp)
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_flock ${FLOCK_BENCH_SRC} ${OL_STRUCT_DIR}/${bench} "OL" "")
endforeach()


# Canonical fuse TLF over OL structures. MV_TLF activates verlib.h's
# MV_TLF branch, which provides verlib::versioned_ptr =
# tlf_internal::versioned_ptr — that type's load_transactional() has
# the real TLF unprotected-read mechanism: skip validate_ptr_log push
# when in_lock == 0 (versioned_ptr.h:50-66). Locks are logged into the
# transaction descriptor and taken at commit (lock fusion via
# tlf_internal::lock::try_lock). This is the published SPAA'25
# implementation; included for sanity checking the uSTM TLF integration
# (${bench}_ol_ustm_tlf).
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_ol_fuse ${MIXED} ${OL_STRUCT_DIR}/${bench} "Other;Fuse;MV_TLF;OL" "")
endforeach()

# Transactional-wrapping variant of the OL benchmarks using uSTM with
# the TLF (Transactional Lock Fusion) counter protocol. Same plain
# blocking OL structures as ${bench}_ol but:
#   - stm::atomic_region    = ustm::transaction (mvustm_glue.h via USTM)
#   - stm::atomic / lock    = uSTM's transactional atomic + tlf_lock
#                             (routed in via OL_USE_STM in verlib.h)
#   - flck::lock try_lock   = bump inProtected counter, no real acquire
#   - load() inside txn but outside an OL protected region skips readLog
#
# This is the counter-variant TLF described in SPAA'25 §6.2 — OL locks
# serve only as protected-region markers, the STM's commit-time write
# locks provide mutual exclusion. Lock-log variant is not implemented
# (would require bypassing uSTM's per-location seq-lock on reads).
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_ol_ustm_tlf ${MIXED} ${OL_STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;OL;OL_USE_STM;USTM_TLF" "")
endforeach()

# Companion to ${bench}_ol_ustm_tlf: same uSTM backend and TLF lock
# wrapper, but USTM_TLF is dropped. Without USTM_TLF, every read inside
# a transaction is pushed to readLog regardless of inProtected — i.e.,
# "uSTM TLF without weak loads". Lets us isolate the value of the
# unprotected-read optimization on its own.
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_ol_ustm ${MIXED} ${OL_STRUCT_DIR}/${bench} "USTM;MV;SplitStamp;OL;OL_USE_STM" "")
endforeach()

# Hardware-timestamp variants of the OL TLF benchmarks (x86 only).
# RDTSC-based global timestamp eliminates the commit-time CAS contention
# on a single shared atomic, which is a major bottleneck at high thread
# counts. The published Fuse / mvustm experiments use these; included
# here so the txn_overhead sweep can attribute overhead to the stamp
# scheme separately from the TLF protocol.
if (TARGET_ARCH MATCHES "x86")
  foreach(bench ${OL_STRUCTS})
    add_benchmark(${bench}_ol_fuse_hwstamp ${MIXED} ${OL_STRUCT_DIR}/${bench} "Other;Fuse;MV_TLF;HWWriteStamp;OL" "")
    add_benchmark(${bench}_ol_ustm_tlf_hwstamp ${MIXED} ${OL_STRUCT_DIR}/${bench} "USTM;MV;HWStamp;OL;OL_USE_STM;USTM_TLF" "")
    add_benchmark(${bench}_ol_ustm_hwstamp ${MIXED} ${OL_STRUCT_DIR}/${bench} "USTM;MV;HWStamp;OL;OL_USE_STM" "")
  endforeach()

  # Pure-STM variants of the OL benchmark — every load/store goes through
  # tlf_internal::lock's read_lock / try_lock machinery (fuse/stm.h via
  # verlib.h's MV_STM branch). No weak loads on traversal pointers; reads
  # always counter-bump in_lock and stores always lock_log. WeakLoad is
  # defined explicitly so atomic_bool's removed flag works (without it the
  # MV_STM atomic_bool::load() returns false unconditionally).
  foreach(bench ${OL_STRUCTS})
    add_benchmark(${bench}_ol_fuse_stm ${MIXED} ${OL_STRUCT_DIR}/${bench} "Other;Fuse;MV_STM;WeakLoad;OL" "")
    add_benchmark(${bench}_ol_fuse_stm_hwstamp ${MIXED} ${OL_STRUCT_DIR}/${bench} "Other;Fuse;MV_STM;WeakLoad;HWWriteStamp;OL" "")
  endforeach()
endif()

# Multiverse on OL data structures. Same OL_USE_STM routing as
# ol_ustm: structures' versioned_ptr / atomic_bool / lock alias to
# ::stm::atomic / lock, which multiverse_glue.hpp provides backed by
# ns_multiverse::tx_field. Multiverse is intrinsically MV; no TLF
# integration needed.
foreach(bench ${OL_STRUCTS})
  add_benchmark(${bench}_ol_multiverse ${MIXED} ${OL_STRUCT_DIR}/${bench} "MAX_THREADS_POW2=256;CPU_FREQ_GHZ=2.4;MULTIVERSE;OL;OL_USE_STM" "")
endforeach()

#benchmarks incompatible with arm
if (NOT TARGET_ARCH MATCHES "arm")
  foreach(bench ${STRUCTS})
    add_benchmark(${bench}_tiny ${MIXED} ${STRUCT_DIR}/${bench} "Other;Tiny" tinystm)
  endforeach()

  add_benchmark(tpcc_mix_hytm ${TPCC} "${STRUCT_DIR}/arttree;${STRUCT_DIR}/hash_block" "Other;HyTm;Mix" "")

  foreach(bench ${SUB_TPCC_STRUCTS})
    add_benchmark(tpcc_${bench}_hytm ${TPCC} ${STRUCT_DIR}/${bench} "Other;HyTm" "")
  endforeach()
endif()
