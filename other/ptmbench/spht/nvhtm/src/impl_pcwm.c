#include "impl.h"
#include "spins.h"
#include "rdtsc.h"

#include <stdio.h>
#include <limits.h>
#include <stdint.h>
#include <errno.h>


// use power2: 0 --> does on every sample: 1 --> does on none
// #define PCWM_NB_SAMPLES 0

#define LARGER_THAN(_TSi, _TSj, _i, _j) ((_TSi > _TSj) || (_TSi == _TSj && _i > _j))

typedef uintptr_t bit_array_t;

static volatile __thread uint64_t readClockVal;

static volatile __thread
  int writeLogStart, writeLogEnd;

static volatile __thread int canJumpToWait = 0;

// static volatile __thread uint64_t nbSamples;
// static volatile __thread uint64_t nbSamplesDone;

static volatile __thread uint64_t timeFlushTS1 = 0;
static volatile __thread uint64_t timeFlushTS2 = 0;

static volatile __thread uint64_t timeWaitingTS1 = 0;
static volatile __thread uint64_t timeWaitingTS2 = 0;
static volatile __thread uint64_t timeWaiting = 0;
static volatile __thread uint64_t timeFlushing = 0;
static volatile __thread uint64_t timeTX = 0;

static volatile __thread uint64_t countCommitPhases = 0;

// static volatile uint64_t incNbSamples = 0;
static volatile uint64_t incCommitsPhases = 0;
static volatile uint64_t incTimeTotal = 0;
static volatile uint64_t incAfterTx = 0;
static volatile uint64_t incWaiting = 0;
static volatile uint64_t incFlushing = 0;
static volatile uint64_t incTX = 0;

void install_bindings_pcwm()
{
  on_before_htm_begin  = on_before_htm_begin_pcwm;
  on_htm_abort         = on_htm_abort_pcwm;
  on_before_htm_write  = on_before_htm_write_8B_pcwm;
  on_before_htm_commit = on_before_htm_commit_pcwm;
  on_after_htm_commit  = on_after_htm_commit_pcwm;

  wait_commit_fn = wait_commit_pcwm;
}

static inline void fetch_log(int threadId)
{
  write_log_thread[writeLogEnd] = 0;
  write_log_thread[(writeLogEnd + 8) & (gs_appInfo->info.allocLogSize - 1)] = 0;
}

void on_before_htm_begin_pcwm(int threadId)
{
  onBeforeWrite = on_before_htm_write;
  onBeforeHtmCommit = on_before_htm_commit;
  write_log_thread = &(P_write_log[threadId][0]);

  // nbSamples++;
  // if ((nbSamples & (PCWM_NB_SAMPLES - 1)) == (PCWM_NB_SAMPLES - 1)) {
  //   nbSamplesDone++;
  // }
  
  writeLogEnd = writeLogStart = G_next[threadId].log_ptrs.write_log_next;
  fetch_log(threadId);

  if (log_replay_flags & LOG_REPLAY_CONCURRENT) {
    // check space in the log
    const int MIN_SPACE_IN_LOG = 128;
    long totSize = gs_appInfo->info.allocLogSize;
    volatile long start = G_next[threadId].log_ptrs.write_log_start;
    long next = G_next[threadId].log_ptrs.write_log_next;
    long nextExtra = (next + MIN_SPACE_IN_LOG) & (totSize - 1);
    volatile long size = next >= start ? next - start : totSize - (start - next);
    long extraSize = nextExtra > next ? nextExtra - next : totSize - (next - nextExtra);
    while (size + extraSize > totSize) {
      // wait the background replayer to gather some transactions
      start = __atomic_load_n(&G_next[threadId].log_ptrs.write_log_start, __ATOMIC_ACQUIRE);
      size = next >= start ? next - start : totSize - (start - next);
      extraSize = nextExtra > next ? nextExtra - next : totSize - (next - nextExtra);
    }
  }

  __atomic_store_n(&gs_ts_array[threadId].pcwm.isUpdate, 0, __ATOMIC_RELEASE);
  __atomic_store_n(&gs_ts_array[threadId].pcwm.ts, rdtsc(), __ATOMIC_RELEASE);
}

void on_htm_abort_pcwm(int threadId)
{
  __atomic_store_n(&gs_ts_array[threadId].pcwm.ts, rdtsc(), __ATOMIC_RELEASE);
}

void on_before_htm_write_8B_pcwm(int threadId, void *addr, uint64_t val)
{
  write_log_thread[writeLogEnd] = (uint64_t)addr;
  writeLogEnd = (writeLogEnd + 1) & (gs_appInfo->info.allocLogSize - 1);
  write_log_thread[writeLogEnd] = (uint64_t)val;
  writeLogEnd = (writeLogEnd + 1) & (gs_appInfo->info.allocLogSize - 1);
}

void on_before_htm_commit_pcwm(int threadId)
{
  readClockVal = rdtscp();
}

static inline void smart_close_log_pcwm(uint64_t marker, uint64_t *marker_pos)
{
  intptr_t lastCL  = ((uintptr_t)(&write_log_thread[writeLogEnd]) >> 6) << 6;
  intptr_t firstCL = ((uintptr_t)(&write_log_thread[writeLogStart]) >> 6) << 6;

  void *logStart = (void*) (write_log_thread + 0);
  void *logEnd   = (void*) (write_log_thread + gs_appInfo->info.allocLogSize);

  if (firstCL == lastCL) {
    // same cache line
    *marker_pos = marker;
    FLUSH_RANGE(firstCL, lastCL, logStart, logEnd);
  } else {
    intptr_t lastCLMinus1;
    if (lastCL == (uintptr_t)logStart) {
      lastCLMinus1 = (uintptr_t)logEnd;
    } else {
      lastCLMinus1 = lastCL - 1;
    }
    FLUSH_RANGE(firstCL, lastCLMinus1, logStart, logEnd);
    FENCE_PREV_FLUSHES();
    *marker_pos = marker;
    FLUSH_CL((void*)lastCL);
  }
}

void on_after_htm_commit_pcwm(int threadId)
{
  INC_PERFORMANCE_COUNTER(timeTotalTS1, timeAfterTXTS1, timeTX);
  int didTheFlush = 0;

  // gs_ts_array[threadId].pcwm.ts = readClockVal; // currently has the TS of begin
  // tells the others my TS taken within the TX
  // TODO: remove the atomic
  __atomic_store_n(&gs_ts_array[threadId].pcwm.ts, readClockVal, __ATOMIC_RELEASE);

  // printf("[%i] did TX=%lx\n", threadId, readClockVal);

  if (writeLogStart == writeLogEnd) {
    // tells the others to move on
    __atomic_store_n(&gs_ts_array[threadId].pcwm.ts, onesBit63(readClockVal), __ATOMIC_RELEASE);
    goto ret;
  }

  __atomic_store_n(&gs_ts_array[threadId].pcwm.isUpdate, 1, __ATOMIC_RELEASE);

#ifndef DISABLE_PCWM_OPT
  // says to the others that it intends to write this value in the marker
  // TODO: now using gs_ts_array[threadId].pcwm.ts as TS intention
  // __atomic_store_n(&gs_ts_array[threadId].comm2.globalMarkerIntent, readClockVal, __ATOMIC_RELEASE);
#endif

  // if ((nbSamples & (PCWM_NB_SAMPLES - 1)) == (PCWM_NB_SAMPLES - 1)) {
    MEASURE_TS(timeFlushTS1);
  // }

  // int prevWriteLogEnd = writeLogEnd;
  // writeLogEnd = (writeLogEnd + 1) & (gs_appInfo->info.allocLogSize - 1); // needed
  smart_close_log_pcwm(
    /* commit value */ onesBit63(readClockVal),
    /* marker position */ (uint64_t*)&(write_log_thread[writeLogEnd])
  );
  // writeLogEnd = prevWriteLogEnd;

  // -------------------
  /** OLD */
  // flush log entries
  // writeLogEnd = (writeLogEnd + gs_appInfo->info.allocLogSize - 1) & (gs_appInfo->info.allocLogSize - 1);
  // FLUSH_RANGE(&write_log_thread[writeLogStart], &write_log_thread[writeLogEnd],
  //   &write_log_thread[0], write_log_thread + gs_appInfo->info.allocLogSize);
  // writeLogEnd = (writeLogEnd + 1) & (gs_appInfo->info.allocLogSize - 1);
  // FENCE_PREV_FLUSHES();
  // /* Commits the write log (commit marker) */
  // write_log_thread[writeLogEnd] = onesBit63(readClockVal);
  // FLUSH_CL(&write_log_thread[writeLogEnd]);
  /** OLD */
  // -------------------

  // tells the others flush done!
  FENCE_PREV_FLUSHES();
  __atomic_store_n(&gs_ts_array[threadId].pcwm.ts, onesBit63(readClockVal), __ATOMIC_RELEASE);

  // if ((nbSamples & (PCWM_NB_SAMPLES - 1)) == (PCWM_NB_SAMPLES - 1)) {
    MEASURE_TS(timeFlushTS2);
  // }
  INC_PERFORMANCE_COUNTER(timeFlushTS1, timeFlushTS2, timeFlushing);

  // now: is it safe to return to the application?
  // first wait preceding TXs
  canJumpToWait = 0;
  wait_commit_fn(threadId);
  // all preceding TXs flushed their logs, and we know the intention

  if (canJumpToWait) goto waitTheMarker;

  // second verify it the checkpointer will reproduce our log
  volatile uint64_t oldVal, oldVal2;
// putTheMarker: //GUY:: this causing unused warnings seems like they commented out its use
  if ((oldVal = __atomic_load_n(&P_last_safe_ts->ts, __ATOMIC_ACQUIRE)) < readClockVal) {
    int success = 0;
    // it will not be reproduced, need to change it
    while (__atomic_load_n(&P_last_safe_ts->ts, __ATOMIC_ACQUIRE) < readClockVal) {
      oldVal2 = __sync_val_compare_and_swap(&P_last_safe_ts->ts, oldVal, readClockVal);
      success = (oldVal2 == oldVal);
      oldVal = oldVal2;
    }
    if (success) {
      FLUSH_CL(&P_last_safe_ts->ts);
      FENCE_PREV_FLUSHES();
      didTheFlush = 1;
// -------------------------------
      // tells the others that I've managed to flush up to my TS
      // __atomic_store_n(&gs_ts_array[threadId].comm2.globalMarkerTS, readClockVal, __ATOMIC_RELEASE);
      // TODO: now using the same cacheline
      __atomic_store_n(&gs_ts_array[threadId].pcwm.flushedMarker, readClockVal, __ATOMIC_RELEASE);
// -------------------------------
      // this may fail, I guess a more recent transaction will update the TS...
      // at this point it should be guaranteed that the checkpointer sees:
      //  P_last_safe_ts->ts >= readClockVal (it ignores bit 63)
      // TODO: while enabling this do not forget zeroBit63 whenever you read P_last_safe_ts
      // __sync_val_compare_and_swap(&P_last_safe_ts->ts, readClockVal, onesBit63(readClockVal));
    }
  }
waitTheMarker:
  if (!didTheFlush) {
    // tried the following (seems to be slightly faster):
// -------------------------------
    volatile uint64_t spinCount = 0;
    while (1) {
      // while (__atomic_load_n(&P_last_safe_ts->ts, __ATOMIC_ACQUIRE) < readClockVal) {
      //   _mm_pause();
      //   spinCount++;
      //   if (spinCount > 10000) {
      //     // printf("goto putTheMarker\n");
      //     goto putTheMarker;
      //   }
      // }; // need to wait
      spinCount++;
      for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) {
            // if (gs_ts_array[i].comm2.globalMarkerTS >= readClockVal) {
        if (__atomic_load_n(&gs_ts_array[i].pcwm.flushedMarker, __ATOMIC_ACQUIRE) >= readClockVal) {
          goto outerLoop;
        }
      }
      if (spinCount > 10000) {
        // goto putTheMarker;
      }
      _mm_pause();
    }
// -------------------------------
    // // need to be sure it was flushed
    // while (!isBit63One(P_last_safe_ts->ts)); // TODO: the code before seems slightly faster
  }
outerLoop:
  
  G_next[threadId].log_ptrs.write_log_next = (writeLogEnd + 1) & (gs_appInfo->info.allocLogSize - 1);
ret:
  MEASURE_INC(countCommitPhases);
}

void wait_commit_pcwm(int threadId)
{
  // if ((nbSamples & (PCWM_NB_SAMPLES - 1)) == (PCWM_NB_SAMPLES - 1)) {
    MEASURE_TS(timeWaitingTS1);
  // }
  volatile uintptr_t myTS = gs_ts_array[threadId].pcwm.ts;
  volatile uint64_t snapshotTS[gs_appInfo->info.nbThreads];
  volatile uint64_t snapshotDiscard[gs_appInfo->info.nbThreads];

  for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) {
    snapshotDiscard[i] = 0;
  }

  for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) {
    if (i == threadId) continue;
    snapshotTS[i] = gs_ts_array[i].pcwm.ts; // puts too much presure on this cache line
    if (LARGER_THAN(zeroBit63(snapshotTS[i]), myTS, i, threadId) || isBit63One(snapshotTS[i])) {
      snapshotDiscard[i] = 1;
    }
#ifndef DISABLE_PCWM_OPT
    if (LARGER_THAN(zeroBit63(snapshotTS[i]), readClockVal, i, threadId)
        && __atomic_load_n(&gs_ts_array[i].pcwm.isUpdate, __ATOMIC_ACQUIRE) == 1) {
      canJumpToWait = 1;
    }
#endif
  }

  for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) {
    if (i == threadId || snapshotDiscard[i] == 1) continue;
    do {
      snapshotTS[i] = gs_ts_array[i].pcwm.ts;
      // _mm_pause();
    } while (!(LARGER_THAN(zeroBit63(snapshotTS[i]), myTS, i, threadId) || isBit63One(snapshotTS[i]))
       && snapshotTS[i] != 0 
      /* && !gs_appInfo->info.isExit */);
  }

  // if ((nbSamples & (PCWM_NB_SAMPLES - 1)) == (PCWM_NB_SAMPLES - 1)) {
    MEASURE_TS(timeWaitingTS2);
    INC_PERFORMANCE_COUNTER(timeWaitingTS1, timeWaitingTS2, timeWaiting);
  // }
}

void state_gather_profiling_info_pcwm(int threadId)
{
  // __sync_fetch_and_add(&incCommitsPhases, countCommitPhases);
  // // __sync_fetch_and_add(&incNbSamples, nbSamplesDone);
  // __sync_fetch_and_add(&incTimeTotal, timeTotal);
  // __sync_fetch_and_add(&incAfterTx, timeAfterTXSuc);
  // __sync_fetch_and_add(&incWaiting, timeWaiting);
  // __sync_fetch_and_add(&incFlushing, timeFlushing);
  // __sync_fetch_and_add(&incTX, timeTX);
  // __sync_fetch_and_add(&timeSGL_global, timeSGL);
  // __sync_fetch_and_add(&timeAbortedTX_global, timeAbortedTX);

  // timeSGL = 0;
  // timeAbortedTX = 0;
  // timeTX = 0;
  // timeAfterTXSuc = 0;
  // timeWaiting = 0;
  // timeTotal = 0;
  // countCommitPhases = 0;
}