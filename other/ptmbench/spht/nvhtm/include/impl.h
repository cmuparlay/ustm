#ifndef IMPL_H_GUARD
#define IMPL_H_GUARD

#include "global_structs.h"

#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <errno.h>
#include <setjmp.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

// // #define MEASURE_TS(ts_var) ts_var = rdtscp()
// #define MEASURE_TS(ts_var) ts_var = rdtsc(); __asm__ __volatile__ ("lfence;");
// #define MEASURE_INC(counter) counter++
// #define INC_PERFORMANCE_COUNTER(ts_var1, ts_var2, counter) counter += ts_var2 - ts_var1

#ifdef NPROFILE
#define MEASURE_TS(ts_var) /* empty */
#define MEASURE_INC(counter) /* empty */
#define INC_PERFORMANCE_COUNTER(ts_var1, ts_var2, counter) /* empty */
#else
#define MEASURE_TS(ts_var) ts_var = rdtscp()
#define MEASURE_INC(counter) counter++
#define INC_PERFORMANCE_COUNTER(ts_var1, ts_var2, counter) counter += ts_var2 - ts_var1
#endif

// These are always done
#define A_MEASURE_TS(ts_var) ts_var = rdtscp()
#define A_MEASURE_INC(counter) counter++
#define A_INC_PERFORMANCE_COUNTER(ts_var1, ts_var2, counter) counter += ts_var2 - ts_var1

#ifdef USE_BITWISE_FOR_MOD2
#define MOD_FN(_arg, _mod) ((_arg) & (_mod-1))
#else
#define MOD_FN(_arg, _mod) ((_arg) % (_mod))
#endif

extern __thread long nbTransactions;

#define NV_HTM_BEGIN(_threadId) \
  nbTransactions++;\
  while (1) { /* TODO: this needs to be in the same function (else screws the stack) */ \
    jmp_buf env; \
    MEASURE_TS(timeTotalTS1); \
    on_before_htm_begin(_threadId); \
    HTM_SGL_begin(); \
//

#define NV_HTM_END(_threadId) \
  onBeforeHtmCommit(_threadId); \
  HTM_SGL_commit(); \
  MEASURE_TS(timeAfterTXTS1); \
  on_after_htm_commit(_threadId); \
  MEASURE_TS(timeTotalTS2); \
  INC_PERFORMANCE_COUNTER(timeTotalTS1, timeTotalTS2, timeTotal); \
  INC_PERFORMANCE_COUNTER(timeAfterTXTS1, timeTotalTS2, timeAfterTXSuc); \
  break; \
} \
//






// initialize with this to change the implementation to use
void install_bindings_pcwm();
void install_bindings_pcwm2();
void install_bindings_pcwm3();

/* ************************************************************************** */

typedef enum {
  LOG_REPLAY_FORWARD  = 0b0001,
  LOG_REPLAY_BACKWARD = 0b0010,
  LOG_REPLAY_ASYNC_SORTER = 0b0100,
  LOG_REPLAY_LOGICAL_CLOCKS  = 0b00010000,
  LOG_REPLAY_PHYSICAL_CLOCKS = 0b00100000, // TODO: PCWM make use of the marker
  LOG_REPLAY_BUFFER_FLUSHES  = 0b01000000, // buffers in set then flush all cache lines
  LOG_REPLAY_BUFFER_WBINVD   = 0b10000000, // use the WBINVD instruction in the end (avoid buffer set)
  LOG_REPLAY_PHYSICAL_CLOCKS_SORTED = 0b100000000, // PCWM2
  LOG_REPLAY_RANGE_FLUSHES = 0b1000000000, //
  LOG_REPLAY_CC_HTM = 0b10000000000, //
  LOG_REPLAY_CC_HTM_NO_W = 0b100000000000, //
  LOG_REPLAY_PARALLEL = 0b1000000000000, //
  LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD = 0b10000000000000, // PCWM3
  LOG_REPLAY_CONCURRENT = 0b100000000000000 // concurrent apply
} LOG_REPLAY_FLAGS; 

extern int log_replay_flags;

void replay_log_init(
  int _nbThreads,
  int _nbReplayers,
  uint64_t _logSize,
  volatile cache_line_s *_logPtrs,
  volatile uint64_t **_writeLog,
  volatile uint64_t *_latestSafeTsv/* PCWM */,
  int flags,
  void **_nvramRanges,
  int _nbNvramRanges
);
void replay_log_destroy();
uint64_t replay_log_total_bytes();
void replay_log_print_stats(char *filename);

void replay_log_apply(); // run all

void disablePhtmNoopFuncA(int threadId);
void disablePhtmNoopFuncB(int threadId, void *addr, uint64_t val);

void on_before_htm_begin_pcwm(int threadId);
void on_before_htm_begin_pcwm2(int threadId);
void on_before_htm_begin_pcwm3(int threadId);

void on_htm_abort_pcwm(int threadId);
void on_htm_abort_pcwm2(int threadId);
void on_htm_abort_pcwm3(int threadId);

void on_before_htm_write_8B_pcwm(int threadId, void *addr, uint64_t val);
void on_before_htm_write_8B_pcwm2(int threadId, void *addr, uint64_t val);
void on_before_htm_write_8B_pcwm3(int threadId, void *addr, uint64_t val);

void on_before_htm_commit_pcwm(int threadId);
void on_before_htm_commit_pcwm2(int threadId);
void on_before_htm_commit_pcwm3(int threadId);

void on_after_htm_commit_pcwm(int threadId);
void on_after_htm_commit_pcwm2(int threadId);
void on_after_htm_commit_pcwm3(int threadId);

void try_prune_log_epoch_impa(int threadId);
void prune_log_forward_epoch_impa(int threadId); /* called by try_prune_log_impa */

void wait_commit_pcwm(int threadId);
void wait_commit_pcwm2(int threadId);
void wait_commit_pcwm3(int threadId);


void on_before_htm_begin_disable_phtm(int threadId);
void on_htm_abort_disable_phtm(int threadId);
void on_before_htm_commit_disable_phtm(int threadId);
void on_after_htm_commit_disable_phtm(int threadId);
void wait_commit_disable_phtm(int threadId);
void on_before_htm_write_8B__disable_phtm(int threadId, void *addr, uint64_t val);


void state_gather_profiling_info_pcwm(int threadId);
void state_gather_profiling_info_pcwm2(int threadId);
void state_gather_profiling_info_pcwm3(int threadId);

// -----------------------------------------------------------------------------
// steal bit
#define flipBit63(_64b_uint) (( (uint64_t)1 << 63) ^ (_64b_uint))
#define onesBit63(_64b_uint) (( (uint64_t)1 << 63) | (_64b_uint))
#define zeroBit63(_64b_uint) (((uint64_t)-1 >>  1) & (_64b_uint))
#define isBit63One(_64b_uint) ((((uint64_t)1 << 63) & (_64b_uint)) >> 63)
// is flushed bit
#define flipBit62(_64b_uint) (( (uint64_t)1 << 62) ^ (_64b_uint))
#define onesBit62(_64b_uint) (( (uint64_t)1 << 62) | (_64b_uint))
#define zeroBit62(_64b_uint) ((((uint64_t)-1 >> 2) | (uint64_t)1 << 63)) & (_64b_uint))
#define isBit62One(_64b_uint) ((((uint64_t)1 << 62) & (_64b_uint)) >> 62)

#define zeroBit62and63(_64b_uint) (((uint64_t)-1 >> 2) & (_64b_uint))

#define EPOCH_PTR(_tid) (G_next[_tid].padded_ptr.ptr)
#define EPOCH_LAST(threadID) __atomic_load_n(&(EPOCH_PTR(threadID)), __ATOMIC_ACQUIRE)
#define EPOCH_PREVIOUS(threadID) \
  (EPOCH_PTR(threadID) == 0 ? 0 : EPOCH_PTR(threadID) - 1)
#define EPOCH_MINUS2(threadID) \
  (EPOCH_PTR(threadID) < 2 ? EPOCH_PTR(threadID) : EPOCH_PTR(threadID) - 2)
#define EPOCH_EMPTY_PREVIOUS(threadID) \
  (P_epoch_ts[threadID][(MOD_FN(EPOCH_PREVIOUS(threadID), gs_appInfo->info.allocEpochs)] = 0)

#define EPOCH_READ_PTR(ptr, threadID) \
  __atomic_load_n(&(P_epoch_ts[threadID][MOD_FN((ptr), gs_appInfo->info.allocEpochs)]), __ATOMIC_ACQUIRE)
#define EPOCH_READ_PTR_U(ptr, threadID) /* NOTE: the _U does not do modulo */\
  __atomic_load_n(&(P_epoch_ts[threadID][ptr].ts), __ATOMIC_ACQUIRE)
#define EPOCH_READ(threadID) \
  __atomic_load_n(&(P_epoch_ts[threadID][MOD_FN(EPOCH_LAST(threadID), gs_appInfo->info.allocEpochs)]), __ATOMIC_ACQUIRE)
#define EPOCH_READ_NEXT_N(threadID, _n) \
  __atomic_load_n(&(P_epoch_ts[threadID][MOD_FN(((EPOCH_LAST(threadID)+_n), gs_appInfo->info.allocEpochs)]), __ATOMIC_ACQUIRE)
#define EPOCH_READ_BEFORE_PREVIOUS(threadID) \
  __atomic_load_n(&(P_epoch_ts[threadID][MOD_FN(EPOCH_MINUS2(threadID), gs_appInfo->info.allocEpochs)]), __ATOMIC_ACQUIRE)

/* epoch write must also update the log pointers*/
#define EPOCH_WRITE(threadID, tsReading) \
  __atomic_store_n(&(P_epoch_ts[threadID][EPOCH_LAST(threadID)]), tsReading, __ATOMIC_RELEASE); \
  FLUSH_CL(&(P_epoch_ts[threadID][EPOCH_LAST(threadID)]));
#define EPOCH_WRITE_VAL(threadID, tsReading, _epoch) \
  __atomic_store_n(&(P_epoch_ts[threadID][_epoch]), tsReading, __ATOMIC_RELEASE); \
  FLUSH_CL(&(P_epoch_ts[threadID][_epoch]));
#define EPOCH_FINALIZE(threadID) \
  EPOCH_PTR(threadID) = MOD_FN((EPOCH_PTR(threadID) + 1), gs_appInfo->info.allocEpochs)

// -----------------------------------------------------------------------------
#define IMPATIENT_EPOCH_NEXT_INC(threadID) \
  (EPOCH_PTR(threadID) = MOD_FN((EPOCH_PTR(threadID) + 1), gs_appInfo->info.allocEpochs))
#define IMPATIENT_EPOCH_END_INC(pruned)                                   \
  __atomic_store_n(                                                       \
    &gs_log_data.log.epoch_end,                                           \
    MOD_FN((gs_log_data.log.epoch_end + (pruned)), gs_appInfo->info.allocEpochs), \
    __ATOMIC_RELEASE);

#define IMPATIENT_EPOCH_READ(threadID) EPOCH_READ(threadID)

// attempts to reserve own epoch slot with read TS
#define TRY_CAS_EPOCH_SLOT(_tid, _slot, _readTS, _ts) \
  __sync_bool_compare_and_swap(&(P_epoch_ts[_tid][_slot]), _readTS, _ts)

// if (_epoch == P_start_epoch) use other approach
#define IS_SLOT_OCCUPIED_NON_START(_epoch, _tid) \
    (zeroBit62and63(P_epoch_ts[_tid][_epoch]) > zeroBit62and63(P_epoch_ts[_tid][P_start_epoch]))

// TODO: check with the prev: when _epoch == P_start_epoch it does not work!!!
#define IS_SLOT_OCCUPIED_START(_epoch, _tid) \
  zeroBit62and63(P_epoch_ts[_tid][MOD_FN((_epoch + gs_appInfo->info.allocEpochs - 1), \
  gs_appInfo->info.allocEpochs)]) < zeroBit62and63(P_epoch_ts[_tid][_epoch])

#define IS_SLOT_OCCUPIED(_epoch1, _tid1) ({ \
  int _res = 0; \
  if (_epoch1 == P_start_epoch) { \
    _res = IS_SLOT_OCCUPIED_START(_epoch1, _tid1); \
  } else { \
    _res = IS_SLOT_OCCUPIED_NON_START(_epoch1, _tid1); \
  } \
  _res; \
})

// TS version avoids loading the remote TS multiple times
#define IS_SLOT_OCCUPIED_TS_NON_START(_epoch, _tid, _ts) \
    (_ts > zeroBit62and63(P_epoch_ts[_tid][P_start_epoch]))

#define IS_SLOT_OCCUPIED_TS_START(_epoch, _tid, _ts) \
  (zeroBit62and63(P_epoch_ts[_tid][MOD_FN((_epoch + gs_appInfo->info.allocEpochs - 1), \
  gs_appInfo->info.allocEpochs)]) < _ts)

#define IS_SLOT_OCCUPIED_TS(_epoch1, _tid1, _ts1) ({ \
  int _res = 0; \
  if (_epoch1 == P_start_epoch) { \
    _res = IS_SLOT_OCCUPIED_TS_START(_epoch1, _tid1, _ts1); \
  } else { \
    _res = IS_SLOT_OCCUPIED_TS_NON_START(_epoch1, _tid1, _ts1); \
  } \
  _res; \
})

#define IS_SLOT_UNSYNC(_epoch, _tid, _myTid) \
  (zeroBit62and63(P_epoch_ts[_tid][_epoch]) < zeroBit62and63(P_epoch_ts[_myTid][ \
    (_epoch + gs_appInfo->info.allocEpochs - 1) % gs_appInfo->info.allocEpochs]))

// TODO: log boundaries
#define IS_CLOSE_TO_END(_epochPtr, _endPtr) ( \
  (_epochPtr) == (_endPtr) \
  || MOD_FN(((_epochPtr) + 1), gs_appInfo->info.allocEpochs) == (_endPtr) \
  || MOD_FN(((_epochPtr) + 2), gs_appInfo->info.allocEpochs) == (_endPtr) \
  || MOD_FN(((_epochPtr) + 3), gs_appInfo->info.allocEpochs) == (_endPtr) \
  || MOD_FN(((_epochPtr) + 4), gs_appInfo->info.allocEpochs) == (_endPtr) \
  || MOD_FN(((_epochPtr) + 5), gs_appInfo->info.allocEpochs) == (_endPtr) \
)
//

// does not block
#define LOOK_UP_FREE_SLOT(_tid) ({ \
  int32_t * volatile _slotPtr = (int32_t * volatile)&G_next[_tid].log_ptrs.epoch_next; \
  volatile int32_t _resSlot = *_slotPtr; \
  volatile int32_t _epochEnd = gs_log_data.log.epoch_end; \
  for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) { /* finds the maximum */ \
    volatile int32_t _remoteEpoch = G_next[i].log_ptrs.epoch_next; \
    if ((_resSlot > _epochEnd && _remoteEpoch > _epochEnd) || (_resSlot < _epochEnd && _remoteEpoch < _epochEnd)) { \
      if (_resSlot < _epochEnd) \
        _resSlot = MOD_FN(_remoteEpoch + gs_appInfo->info.allocEpochs -1, gs_appInfo->info.allocEpochs); \
    } else if (_resSlot > _epochEnd && _remoteEpoch < _epochEnd) \
      _resSlot = MOD_FN(_remoteEpoch + gs_appInfo->info.allocEpochs -1, gs_appInfo->info.allocEpochs); \
  } \
  while (IS_SLOT_OCCUPIED(_resSlot, _tid) && !gs_appInfo->info.isExit) { \
    if (IS_CLOSE_TO_END(_resSlot, gs_log_data.log.epoch_end) && !gs_appInfo->info.isExit) { \
      break; \
    } \
    _resSlot = MOD_FN((_resSlot + 1), gs_appInfo->info.allocEpochs); \
  } \
  __atomic_store_n(_slotPtr, _resSlot, __ATOMIC_RELEASE); \
  _resSlot; \
})

#define IS_EPOCH_AFTER(_epochBase, _epochToCheck) ({ \
  int _res2 = 0; \
  if ((_epochBase >= gs_log_data.log.epoch_end && _epochToCheck >= gs_log_data.log.epoch_end) \
      || (_epochBase <= gs_log_data.log.epoch_end && _epochToCheck <= gs_log_data.log.epoch_end)) { \
    _res2 = _epochToCheck > _epochBase; \
  } \
  else if (_epochBase > gs_log_data.log.epoch_end && _epochToCheck < gs_log_data.log.epoch_end) { \
    _res2 = 1; /* _tsToCheck is ahead, after the wrap */ \
  } \
  else if (_epochBase <= gs_log_data.log.epoch_end && _epochToCheck >= gs_log_data.log.epoch_end) { \
    _res2 = 0; /* _tsToCheck is behind, before the wrap */ \
  } \
  _res2; \
}) \
//

#define FIND_LAST_SAFE_EPOCH() ({ \
  volatile int32_t _lastEpoch = __atomic_load_n(&G_next[0].log_ptrs.epoch_next, __ATOMIC_ACQUIRE); \
  for (int _i = 1; _i < gs_appInfo->info.nbThreads; ++_i) { \
    if (!IS_EPOCH_AFTER(_lastEpoch, __atomic_load_n(&G_next[_i].log_ptrs.epoch_next, __ATOMIC_ACQUIRE))) { \
      _lastEpoch = G_next[_i].log_ptrs.epoch_next; \
    } \
  } \
  _lastEpoch = MOD_FN((_lastEpoch + gs_appInfo->info.allocEpochs - 1), gs_appInfo->info.allocEpochs); \
  _lastEpoch; \
})

// -----------------------------------------------------------------------------

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* IMPL_H_GUARD */
