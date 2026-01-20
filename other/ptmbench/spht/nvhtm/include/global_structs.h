#ifndef GLOBAL_STRUCTS_H_GUARD
#define GLOBAL_STRUCTS_H_GUARD

#include "htm_arch.h" // ARCH_CACHE_LINE_SIZE

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif


#ifndef EASY_MALLOC
#define MALLOC_FN(size) aligned_alloc(ARCH_CACHE_LINE_SIZE, (size))
#define EASY_MALLOC(var, nb) \
  if ((var = (__typeof__(var))MALLOC_FN(sizeof(__typeof__(*(var)))*(nb))) == NULL) \
    perror("malloc");
#endif /* EASY_MALLOC */




typedef union cache_line_ {
  struct {
    volatile uint64_t ts;
    volatile uint64_t LLC;
    volatile uint64_t LPC;
    volatile uint64_t version;
    volatile uint64_t isInTX;
    volatile uint64_t padding[3];
  } __attribute__((packed)) comm;
  struct {
    volatile uint64_t ts;
    volatile uint64_t flushedMarker;
    volatile uint64_t stableTS;
    volatile uint64_t stableTSLogPos;
    volatile uint64_t prevStableTSLogPos;
    volatile uint64_t isUpdate;
    volatile uint64_t padding[2];
  } __attribute__((packed)) pcwm;
  struct {
    volatile uint64_t ts;
        volatile uint64_t isInTX;
    volatile uint64_t padding[6];
  } __attribute__((packed)) comm2;
  struct {
    volatile uint64_t ptr;
    volatile uint64_t observedTS;
    volatile uint64_t padding[6];
  } __attribute__((packed)) padded_ptr;
  struct {
    volatile int32_t who_is_pruning;
    volatile int32_t epoch_end;
    volatile uint64_t padding[7];
  } __attribute__((packed)) log;
  struct {
    volatile int32_t epoch_next;
    volatile int32_t write_log_next;
    volatile int32_t flush_epoch;
    volatile int32_t write_log_start;
    volatile uint64_t padding[6];
  } __attribute__((packed)) log_ptrs;
  // struct {
  //   volatile int32_t epoch_next;
  //   volatile int32_t write_log_next;
  //   volatile int32_t flush_epoch;
  //   volatile int32_t write_log_start;
  //   volatile uint64_t padding[6];
  // } __attribute__((packed)) log_ptrs;
  struct {
    int32_t isExit;
    int32_t nbThreads;
    int32_t nbReplayers;
    int32_t allocEpochs;
    int32_t allocLogSize;
    int32_t localMallocSize;
    int32_t GUY_UNUSED_spinsFlush;
    int32_t epochTimeout;
  } __attribute__((packed)) info;
  volatile uint64_t ts; /* padded TS */
  volatile uint64_t padding[8]; /* >64B, TODO: lots of aborts using 8 */
} __attribute__((aligned(ARCH_CACHE_LINE_SIZE))) cache_line_s;

typedef union large_cache_line_ {
  struct {
    volatile uint64_t ts;
    volatile uint64_t LLC;
    volatile uint64_t LPC;
    volatile uint64_t version;
    volatile uint64_t isInTX;
    volatile uint64_t padding[3];
  } __attribute__((packed)) comm;
  struct {
    volatile uint64_t ts;
    volatile uint64_t prevTS;
    volatile uint64_t logPos;
    volatile uint64_t prevLogPos;
    volatile uint64_t flushedMarker;
    volatile uint64_t isUpdate;
    volatile uint64_t padding[2];
  } __attribute__((packed)) pcwm;
  struct {
    volatile uint64_t ts;
    volatile uint64_t isInTX;
    volatile uint64_t isReturnToApp;
    volatile uint64_t globalMarkerTS;
    volatile uint64_t globalMarkerIntent;
    volatile uint64_t isFlushed;
    volatile uint64_t waitSnapshot;
    volatile uint64_t padding[1];
  } __attribute__((packed)) comm2;
  volatile uint64_t ts; /* padded TS */
  volatile uint64_t padding[256];
} __attribute__((aligned(32*ARCH_CACHE_LINE_SIZE))) large_cache_line_s;



typedef void(*wait_commit_fn_t)(int threadId);
typedef void(*prune_log_fn_t)(int upToEpoch);

extern volatile __thread uint64_t timeTotalTS1;
extern volatile __thread uint64_t timeAfterTXTS1;
extern volatile __thread uint64_t timeTotalTS2;
extern volatile __thread uint64_t timeTotal;

extern __thread uint64_t timeSGL_TS1;
extern __thread uint64_t timeSGL_TS2;
extern __thread uint64_t timeSGL;
extern uint64_t timeSGL_global;

extern __thread uint64_t timeAbortedTX_TS1;
extern __thread uint64_t timeAbortedTX_TS2;
extern __thread uint64_t timeAbortedTX;
extern uint64_t timeAbortedTX_global;

extern volatile __thread uint64_t timeAfterTXSuc;
extern volatile __thread uint64_t timeAfterTXFail;

// Put here globably accessible memory
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *gs_ts_array; /* _pc and _lc use this to sync threads */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *G_observed_ts; /* TS obtained within HTM (epoch) */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *P_last_safe_ts; /* TS that indicates the checkpointer up to where it can apply */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **P_epoch_ts; /* persistent */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t *P_epoch_persistent; /* persistent */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **P_write_log; /* persistent, assumes 8B addr and value */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int *P_write_start; /* TODO: start of the write logs */

extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int P_start_epoch; /* persistent */

extern volatile int *G_epoch_lock;

// this has G_epoch_next and G_write_log_next
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s *G_next;

// this has G_who_is_pruning and G_epoch_end
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s gs_log_data;

// this has G_who_is_pruning and G_epoch_end
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s gs_log_pos_info;

// -----------------------------------------------------------------------------
// ### Functions that control NV-HTM behavior
// this must be set after reading command line parameters
extern wait_commit_fn_t wait_commit_fn; // defaults to wait_commit_pc_simple
extern prune_log_fn_t try_prune_log_fn, prune_log_fn;

// -----------------------------------------------------------------------------
// ### Functions to install the current NV-HTM implementation
extern void(*on_htm_abort)(int threadId);
extern void(*on_before_htm_begin)(int threadId);
extern void(*on_before_htm_write)(int threadId, void *addr, uint64_t val);
extern void(*on_before_htm_commit)(int threadId);
extern void(*on_after_htm_commit)(int threadId);

typedef struct replay_log_next_entry_ replay_log_next_entry_s;
extern replay_log_next_entry_s(*log_replay_next_tx_search)();
extern uint64_t(*log_replay_next_tx_apply)(replay_log_next_entry_s curPtr);

// helper inside HTM
extern volatile __thread void(*onBeforeWrite)(int, void*, uint64_t); /* = on_before_htm_write*/
extern volatile __thread void(*onBeforeHtmCommit)(int); /* = on_before_htm_commit */
extern volatile __thread uint64_t *write_log_thread; /* = &(P_write_log[threadId][0]); */

// -----------------------------------------------------------------------------
// ### Flow control
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s *gs_appInfo;

void global_structs_init(
  int nbThreads,
  int nbReplayers,
  uint64_t allocEpochs,
  uint64_t allocLogSize, /* note log must be much larger than allocEpochs */
  uint64_t localMallocSize,
  uint64_t sharedMallocSize,
  char *nvram_regions[]
);

void global_structs_destroy();

void *nvmalloc_init(
  int nbThreads,
  uint64_t logBytesPerThread,
  uint64_t sharedBytes,
  char *nvram_regions[]
);
void *nvmalloc_local(int threadId, size_t bytes);
void *nvmalloc(size_t bytes);
void nvfree(void *ptr);
void nvmalloc_print_stats(char *filename);

// defined in the global_structs.c (TODO: machine dependent)
extern char* NVRAM_REGIONS[];

#ifdef __cplusplus
}
#endif

#endif /* GLOBAL_STRUCTS_H_GUARD */
