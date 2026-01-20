#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE   600
#define _BSD_SOURCE     1
#define _GNU_SOURCE     1
#define _DEFAULT_SOURCE 1

#include "impl.h"
#include "spins.h"

#include <unistd.h>
#include <unordered_set>
#include <unordered_map>
#include <string.h>

#include <iostream>
#include <sched.h>
#include <fcntl.h> //open
#include <unistd.h> //close

#define GUY_ENABLE_REPLAYER_PRINT_OUT

#ifndef MAX_THREAD
#define MAX_THREAD 96
#endif

#ifndef NUM_NUMA_NODES
#define NUM_NUMA_NODES 2
#endif


static void threading_pinThisThread(int coreID)
{
#ifdef __linux__
  cpu_set_t cpu_set;
  CPU_ZERO(&cpu_set);
  CPU_SET(coreID, &cpu_set);
  sched_setaffinity(0, sizeof(cpu_set_t), &cpu_set);
#endif /* __linux__ */
}








#define SORTER_QUEUE_SIZE 8388608

#define LARGER_THAN(_TSi, _TSj, _i, _j) ((_TSi > _TSj) || (_TSi == _TSj && _i > _j))

#define SHARD_FN(addr) 

using namespace std;

// /* DEBUG */static const char log_replay_print_file_name[1024] = "DEBUG_replayer";
// /* DEBUG */static FILE *log_replay_print_file = NULL;

typedef struct replay_log_next_entry_ {
  int threadId;
  long idx;
} replay_log_next_entry_s;

// HACK: defined on global_structs.c
extern volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int32_t *G_flag_checkpointer_exit;

/*extern */int log_replay_flags;

static const long sorter_buffer_size = SORTER_QUEUE_SIZE;
static replay_log_next_entry_s sorter_buffer[sorter_buffer_size];
static intptr_t __attribute__((aligned(ARCH_CACHE_LINE_SIZE*2))) sorter_buffer_cons_idx = 0;
static intptr_t __attribute__((aligned(ARCH_CACHE_LINE_SIZE*2))) sorter_buffer_prod_idx = 0;

static pthread_t sorter;
static int isSorterExit = 0;

static int nbThreads;
static int nbReplayers;
static int nbReplayersLog2;
static uint64_t logSize;
static void **nvramRanges;
static long *nvramRangesOffset;
static volatile int nbNvramRanges;

static volatile uint64_t *latestSafeTsv;
static volatile cache_line_s *logPtrs;
static volatile uint64_t **writeLog;

thread_local int rep_id = 0;

thread_local static uint64_t *curPtrs = NULL;
thread_local static uint64_t *curTSs = NULL;
thread_local static uint64_t *snapshotPtrs = NULL;
static uint64_t **curPtrsThrs; // MAX_NB_THREADS

thread_local static uint64_t curTs = 0;
thread_local static int curThread = -1;

static volatile int replay_is_ready = 0;

static int flags;
thread_local static replay_log_next_entry_s curTx = (replay_log_next_entry_s){-1, -1};

// static uint64_t stats_logSizeBytes;
// static uint64_t stats_nbTxs;
// static uint64_t stats_timeTotal;
// static uint64_t stats_timeApply;
// static uint64_t stats_timeLookup;
// static uint64_t stats_timeBufferEnd;
// static uint64_t stats_countWrites;
// static uint64_t stats_analizedWrites;
// static uint64_t stats_allWrites;
// static uint64_t stats_timeAsyncSorter;
// static uint64_t stats_timeMapInsert;
// static uint64_t stats_timeSetInsert;

static __thread uint64_t countWrites = 0;
static __thread uint64_t analizeWrites = 0;
static __thread uint64_t allWrites = 0;

thread_local static unordered_set<uintptr_t> dirty_cache_lines;
thread_local static unordered_map<uintptr_t, uint8_t> written_cache_lines; // backward

// ------------------- BITSET -------------------
#define BITSET_T uint64_t*

// can be shared because each replayer attacks a different memory page
thread_local static BITSET_T written_cache_lines_bitset; // backward

static inline void BITSET_INIT() { // TODO: put on a MACRO
  size_t bitset_size = 0;
  nvramRangesOffset = (long*)malloc((nbNvramRanges + 1)*sizeof(long));
  nvramRangesOffset[0] = 0;
  for (int i = 0; i < nbNvramRanges; ++i) {
    nvramRangesOffset[i+1] = (uintptr_t)nvramRanges[i*2+1] - (uintptr_t)nvramRanges[i*2];
    bitset_size += nvramRangesOffset[i+1] / 8 / 8 /* each Byte has 8 bits, then 8B words */ +1;
  }
  written_cache_lines_bitset = (uint64_t*)malloc(bitset_size / (nbReplayers == 1 ? 1 : nbReplayers / 2));
  memset(written_cache_lines_bitset, 0, bitset_size / (nbReplayers == 1 ? 1 : nbReplayers / 2));
  // written_cache_lines_bitset = (uint64_t*)malloc(bitset_size);
  // memset(written_cache_lines_bitset, 0, bitset_size);
}

//  ---- NOTE ----
// some positions may overlap, as they will be 
// skipped by threads that are not from the
// corresponding partition
static inline void BITSET_SET(uint64_t addr) {
  uint64_t pos = 0;
  for (int i = 0; i < nbNvramRanges; ++i) {
    if ((uintptr_t)addr >= (uintptr_t)nvramRanges[i*2] && (uintptr_t)addr < (uintptr_t)nvramRanges[i*2+1]) {
      pos = (((uintptr_t)addr - (uintptr_t)nvramRanges[i*2]) + nvramRangesOffset[i]) >> 3; /* div 8*/
    }
  }
  uint64_t pageId = pos >> 9; /* in words */
  uint64_t pageOffset = pos & 0x1FF;
  // TODO: not working 
  pageId = pageId >> nbReplayersLog2; // remove redundant pages
  // pageId = pageId / nbReplayers;
  uint64_t bit = 1L << (pageOffset & 0x3F); /* 1 out of 64 words */
  uint64_t set = (pageId << 3) | (pageOffset >> 6); /* adds the 6 bits of the page offset (64 mem words) */

  // set = pos >> 6; /* div 64 */
  // set = set / nbReplayers;
  // set >>= nbReplayersLog2; // compress for all threads (assumption on the hash function)
  // bit = pos & 0x3F; /* mod 64 */
  written_cache_lines_bitset[set] |= bit;
}

static inline int BITSET_IS_SET(uint64_t addr) {
  uint64_t pos = 0;
  for (int i = 0; i < nbNvramRanges; ++i) {
    if ((uintptr_t)addr >= (uintptr_t)nvramRanges[i*2] && (uintptr_t)addr < (uintptr_t)nvramRanges[i*2+1]) {
      pos = (((uintptr_t)addr - (uintptr_t)nvramRanges[i*2]) + nvramRangesOffset[i]) >> 3; /* word offset */
    }
  }
  uint64_t pageId = pos >> 9; /* in words */
  uint64_t pageOffset = pos & 0x1FF;
  pageId = pageId >> nbReplayersLog2; // remove redundant pages
  // pageId = pageId / nbReplayers;
  uint64_t bit = 1L << (pageOffset & 0x3F); /* 1 out of 64 words */
  uint64_t set = (pageId << 3) | (pageOffset >> 6); /* adds the 6 bits of the page offset (64 mem words) */
  // set = pos >> 6; /* div 64 (location in the bitmap) */
  // set >>= nbReplayersLog2; // compress for all threads (assumption on the hash function)
  // bit = pos & 0x3F; /* mod 64 */
  return (written_cache_lines_bitset[set] & bit) != 0;
}

// ------------------- BITSET -------------------

#define CACHE_LINE_MASK ((uint64_t)-1 << 6) // 6 bit masks 64B

static inline void replay_log_backward_find_maximum_target_ts();
static inline void replay_log_backward_find_minimum_target_ts();
static inline void replay_log_forward_find_minimum_target_ts();
static inline replay_log_next_entry_s replay_log_nextTx_queued();
static inline replay_log_next_entry_s replay_log_nextTx_forward_search_lc();
static inline replay_log_next_entry_s replay_log_nextTx_backward_search_lc();
static inline replay_log_next_entry_s replay_log_nextTx_backward_search_pc(int update);
static inline replay_log_next_entry_s replay_log_nextTx_forward_search_pc();
static inline replay_log_next_entry_s replay_log_nextTx_forward_search_pc_sorted(int useHints, int updateCurTx);
static inline replay_log_next_entry_s replay_log_nextTx_backward_search_pc_sorted(int useHints, int updateCurTx);
static inline uint64_t replay_log_applyTx_backward(replay_log_next_entry_s curPtr, int replayerId);
static inline uint64_t replay_log_applyTx_andAdvance(replay_log_next_entry_s curPtr, int replayerId);
static inline uint64_t replay_log_applyTx_buffer(replay_log_next_entry_s curPtr, int replayerId);
static inline int check_apply(uintptr_t addr, int replayerId);

static void buffer_end()
{
  // if ((flags & LOG_REPLAY_BACKWARD)) {   // ---> TODO: now is the same as WBINVD
  //   for (auto it = written_cache_lines.begin(); it != written_cache_lines.end(); ++it) {
  //     FLUSH_CL((void*)it->first);
  //   }
  // } else
  if ((flags & LOG_REPLAY_BUFFER_FLUSHES)) {
    for (auto it = dirty_cache_lines.begin(); it != dirty_cache_lines.end(); ++it) {
      FLUSH_CL((void*)*it);
    }
    FENCE_PREV_FLUSHES();
  } else if ((flags & LOG_REPLAY_RANGE_FLUSHES)) {
    for (int i = 0; i < nbNvramRanges; ++i) {
      for (uint64_t *ptr = (uint64_t*)nvramRanges[i*2]; ptr < (uint64_t*)nvramRanges[i*2+1]; ptr += 8) {
        FLUSH_CL((void*)ptr);
      }
    }
    FENCE_PREV_FLUSHES();
  } else if ((flags & LOG_REPLAY_BUFFER_WBINVD) || (flags & LOG_REPLAY_BACKWARD)) {
    // FILE *f = fopen("/proc/wbinvd", "r");
    // size_t sz = 0;
    // char * lin = 0;
    // do {
    //   ssize_t lsz = getline(&lin, &sz, f);
    //   // printf("%s\n", lin);
    //   if (lsz < 0) break;
    // } while (!feof(f));
    // fclose (f);
    int wbinvdFd = open("/dev/global_flush", O_RDWR);
    if (wbinvdFd < 0){
      printf("Failed to connect to device\n");
      exit(-1);
    }
    char *buffer = nullptr;
    int ret = write(wbinvdFd, buffer, 0);
    if (ret < 0) {
        printf("Failed to write to the WBINVD device.");
        exit(-1);        
    }
    
    close(wbinvdFd);
    FENCE_PREV_FLUSHES();
  }
}


static void *sorter_fn(void *arg)
{
  snapshotPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curTSs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  memset(snapshotPtrs, 0, nbThreads * sizeof(uint64_t)); // TODO: assumes startPtr = 0
  memset(curPtrs, 0, nbThreads * sizeof(uint64_t)); // TODO: assumes startPtr = 0

  int isDone = 0;
  while (!__atomic_load_n(&replay_is_ready, __ATOMIC_ACQUIRE)) usleep(10);

  // threading_pinThisThread(1);
  long incTXs = 0;
  do {
    replay_log_next_entry_s nextTxEntry;
    // uint64_t ts0, ts1; //unused

    // MEASURE_TS(ts0); //unused
    if ((flags & LOG_REPLAY_BACKWARD)) {
      replay_log_backward_find_maximum_target_ts();
    } else if ((flags & LOG_REPLAY_FORWARD)) {
      replay_log_forward_find_minimum_target_ts();
    }
    do {
      // assume we always have enough space
      // while (sorter_buffer_prod_idx - __atomic_load_n(&sorter_buffer_cons_idx, __ATOMIC_ACQUIRE) >= sorter_buffer_size - 2);
        // printf("sorter waiting!!!\n"); // wait

      if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED)) {
        nextTxEntry = replay_log_nextTx_forward_search_pc_sorted(1, 1);
      } else if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD)) {
        nextTxEntry = replay_log_nextTx_backward_search_pc_sorted(1, 1);
      } else if ((flags & LOG_REPLAY_FORWARD) && (flags & LOG_REPLAY_LOGICAL_CLOCKS)) {
        nextTxEntry = replay_log_nextTx_forward_search_lc();
      } else if ((flags & LOG_REPLAY_FORWARD) && (flags & LOG_REPLAY_PHYSICAL_CLOCKS)) {
        nextTxEntry = replay_log_nextTx_forward_search_pc();
      } else if ((flags & LOG_REPLAY_BACKWARD) && (flags & LOG_REPLAY_LOGICAL_CLOCKS)) {
        nextTxEntry = replay_log_nextTx_backward_search_lc();
      } else if ((flags & LOG_REPLAY_BACKWARD) && (flags & LOG_REPLAY_PHYSICAL_CLOCKS)) {
        nextTxEntry = replay_log_nextTx_backward_search_pc(1);
      } else {
        nextTxEntry = (replay_log_next_entry_s){-1, -1};
      }

      incTXs++;

      if (nextTxEntry.idx == -1) { // break condition
        printf("break at TX = %li, sorter_buffer_prod_idx=%li sorter_buffer_size=%li \n",
          incTXs, sorter_buffer_prod_idx, sorter_buffer_size);
        sorter_buffer[sorter_buffer_prod_idx & (sorter_buffer_size - 1)] = (replay_log_next_entry_s){-1, -1};
        __atomic_store_n(&sorter_buffer_prod_idx, sorter_buffer_prod_idx+1, __ATOMIC_RELEASE);
        isDone = 1;
        break;
      }

      sorter_buffer[sorter_buffer_prod_idx & (sorter_buffer_size - 1)] = nextTxEntry;
      __atomic_store_n(&sorter_buffer_prod_idx, sorter_buffer_prod_idx+1, __ATOMIC_RELEASE);
      // prod_cons_produce(sorterQueue, (void*)&sorter_buffer[idx]);
    } while (1);
    // MEASURE_TS(ts1); //unused
    // INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeAsyncSorter); //unused
    if (isDone) break;
    // wait a bit
    // usleep(100);
  } while(!__atomic_load_n(&isSorterExit, __ATOMIC_ACQUIRE));
  return NULL;
}

int lower_power_of_two(uint64_t v)
{
  uint64_t power = 1;
  int res = 0;
  while (power < v) {
    res++;
    power *= 2;
  }
  return res;
}

void replay_log_init(
  int _nbThreads,
  int _nbReplayers,
  uint64_t _logSize,
  volatile cache_line_s *_logPtrs,
  volatile uint64_t **_writeLog,
  volatile uint64_t *_latestSafeTsv/* PCWM */,
  int _flags,
  void **_nvramRanges,
  int _nbNvramRanges
) {
  nbThreads = _nbThreads;
  nbReplayers = _nbReplayers;

  // TODO: number of threads per NUMA node
  // nbReplayersLog2 = lower_power_of_two(nbReplayers == 1 ? 1 : nbReplayers / NUM_NUMA_NODES);
  nbReplayersLog2 = nbReplayers / NUM_NUMA_NODES;
  printf("nbReplayers: %i nbReplayersLog2: %i\n", nbReplayers, nbReplayersLog2);
  
  logSize = _logSize;
  logPtrs = _logPtrs;
  writeLog = _writeLog;
  latestSafeTsv = _latestSafeTsv;
  flags = _flags;
  nvramRanges = _nvramRanges;
  nbNvramRanges = _nbNvramRanges;

  // log_replay_print_file = fopen(log_replay_print_file_name, "w");

  if ((logSize & (logSize - 1)) != 0) {
    printf("[ERROR]: logSize is not power 2!!! it will not work!!!\n");
  }

  if ((flags & LOG_REPLAY_BACKWARD) && (flags & LOG_REPLAY_FORWARD)) {
    fprintf(stderr, "[ERROR]: cannot setup LOG_REPLAY_BACKWARD and LOG_REPLAY_FORWARD at the same time!\n");
  }

  if ((flags & LOG_REPLAY_LOGICAL_CLOCKS) && (flags & LOG_REPLAY_PHYSICAL_CLOCKS)) {
    fprintf(stderr, "[ERROR]: cannot setup LOG_REPLAY_LOGICAL_CLOCKS and LOG_REPLAY_PHYSICAL_CLOCKS at the same time!\n");
  }

  // TODO: CC-HTM could make use of an async thread as well
  if (flags & LOG_REPLAY_ASYNC_SORTER) {
    // sorterQueue = prod_cons_init(SORTER_QUEUE_SIZE);
    pthread_create(&sorter, NULL, sorter_fn, NULL);
  }

  curPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  snapshotPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curTSs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curPtrsThrs = (uint64_t**)malloc(nbThreads * sizeof(uint64_t*));
  curPtrsThrs[0] = curPtrs;
  memset(curPtrs, 0, nbThreads * sizeof(uint64_t)); // TODO: assumes startPtr = 0
  memset(snapshotPtrs, 0, nbThreads * sizeof(uint64_t)); // TODO: assumes startPtr = 0
  // BITSET_INIT(); // TODO: shared BITMAP
}

void replay_log_destroy()
{
  /* empty */
}

uint64_t replay_log_total_bytes() // TODO: this is giving number of entries, not bytes
{
  uint64_t res = 0;
  for (int i = 0; i < nbThreads; ++i) {
    // assume log start is still at 0
    res += logPtrs[i].log_ptrs.write_log_next;
  } 
  return res;
}

void replay_log_destroy_sorter()
{
  // prod_cons_destroy(sorterQueue);
  __atomic_store_n(&isSorterExit, 1, __ATOMIC_RELEASE);
  pthread_join(sorter, NULL);
}

static inline void fix_next_ptr(int replayerId)
{
if ((flags & LOG_REPLAY_CONCURRENT) && replayerId == 0) {
    if (nbReplayers <= 1) {
      for (int t = 0; t < nbThreads; ++t) {
        if (curPtrs[t] == (uint64_t)-1L) continue;
        __atomic_store_n(&logPtrs[t].log_ptrs.write_log_start, curPtrs[t], __ATOMIC_RELEASE); // <commit marker>
      }
    } else {
      for (int t = 0; t < nbThreads; ++t) {
        volatile uint64_t next = logPtrs[t].log_ptrs.write_log_next;
        volatile uint64_t start = curPtrsThrs[0][t];
        if (start == (uint64_t)-1L) continue;
        // int maxIdx = 0; // GUY:: this is never used 
        long totSize = gs_appInfo->info.allocLogSize;
        uint64_t maxSize = next >= start ? next - start : totSize - (start - next);
        for (int i = 1; i < nbReplayers; ++i) {
          start = __atomic_load_n(&curPtrsThrs[i][t], __ATOMIC_ACQUIRE);
          uint64_t size = next >= start ? next - start : totSize - (start - next);
          if (size > maxSize) {
            // maxIdx = i; //GUY:: This is never used
            maxSize = size;
          }
        }
        // if (logPtrs[t].log_ptrs.write_log_start != start) {
        //   printf("set start ptr of thr%i = %li\n", t, start);
        // }
        __atomic_store_n(&logPtrs[t].log_ptrs.write_log_start, start, __ATOMIC_RELEASE); // <commit marker>
      }
    }
    // logPtrs[curPtr.threadId].log_ptrs.write_log_start = (idx + 1) & (logSize - 1); // <commit marker>
  }
}

static void internal_replay_log_apply(int replayerId)
{
  replay_log_next_entry_s tx;
  // uint64_t tsNext0, tsNext1;
  // uint64_t /*tsApply0, */tsApply1;
  // uint64_t tsBufferEnd0, tsBufferEnd1;
  // uint64_t ts0, ts1;
  // uint64_t tempTotalTime = 0;
  // uint64_t sorterTime = 0, applyTime = 0, bufferEndTime = 0;

#ifdef GUY_ENABLE_REPLAYER_PRINT_OUT
  printf("replayer %i started (flags = %x)\n", replayerId, flags);
#endif
  rep_id = replayerId;

  // uint64_t test_lastTs = 0, test_curTs;

  /*
  // int targetCore = (replayerId ^ 1) / 2 + ((replayerId ^ 1) % 2) * 16; // NUMA remote accesses

  int targetCore = replayerId / 2 + (replayerId % 2) * 16; // NUMA local accesses
  // int targetCore = replayerId / 2 + (replayerId % 2) * 32; // SMT

  // TODO: be carefull with this:
  // int targetCore = replayerId; // fill CPU, then NUMA then SMT
  */
  int targetCore = replayerId; // fill CPU, then NUMA then SMT
  
  //GUY:: not really clear what they are trying to with the above targetCore pinning
  // the paper says they pin the replayers in a round robin fashion such that
  // they swap between NUMA node 0 and NUAM node 1 
  // this is puzzling because elsewhere they appear to use a max thread count of 64
  // which I assumed to be because of hardware thread caps however that is not the case
  // for the machine noted in the paper which has 32 hardware threads  
  // what I have written below is a function that will bounce between 
  // numa node 0 and numa node 1 for a machine with 96 hardware threads
  // where the pinning for numa node 0 is 0-23,48-71,
  // and the pinning for numa node 1 is 24-47,72-95
  // int targetCore = replayerId / 2 + (replayerId % 2) * (MAX_THREAD / 4);
  

  if (flags & LOG_REPLAY_CONCURRENT) {
    //GUY:: removing the clearly hard coded thread numbers here
    // threading_pinThisThread(63 - targetCore);
    threading_pinThisThread((MAX_THREAD - 1) - targetCore);    
  } else {
    threading_pinThisThread(targetCore);
  }
  if (replayerId == 0) {
    // stats_logSizeBytes = replay_log_total_bytes();
  }
  
  // A_MEASURE_TS(ts0);

  if ((flags & LOG_REPLAY_FORWARD) && !(flags & LOG_REPLAY_ASYNC_SORTER)) {

    // check how close we are to the end --> maybe some thread managed to move the next ptr out of order
    // TODO: if LOG_REPLAY_CONCURRENT

    replay_log_forward_find_minimum_target_ts();
  }

  if ((flags & LOG_REPLAY_BACKWARD) && !(flags & LOG_REPLAY_ASYNC_SORTER)) {
    // test_lastTs = (uintptr_t)-1;
    if (flags & LOG_REPLAY_CONCURRENT) {
      replay_log_backward_find_minimum_target_ts();
    } else {
      replay_log_backward_find_maximum_target_ts();
    }
  }

  if (!(flags & LOG_REPLAY_ASYNC_SORTER)) {
    if (flags & LOG_REPLAY_BACKWARD) {
      BITSET_INIT();
      // written_cache_lines.reserve(60000000);
    }
    if (flags & LOG_REPLAY_BUFFER_FLUSHES) {
      dirty_cache_lines.reserve(60000000);
    }
  }

  // uint64_t isRepeat = 0; //GUY:: This is never used, seems like they commented out all of its uses
  uint64_t concWBINVD = 0;
  uint64_t snapWBINVD = 0;
  const uint64_t writesThreshold = 1048576; // 1024

  do {
    int isExit = __atomic_load_n(G_flag_checkpointer_exit, __ATOMIC_ACQUIRE);

    fix_next_ptr(replayerId);

    // check how close we are to the end
    // TODO: This solution is not correct! the pointer that externalizes the
    // log to the checkpointer should not exist!
    if ((flags & LOG_REPLAY_CONCURRENT) && !isExit) {
      const int MIN_SPACE_IN_LOG = 64;
      long totSize = gs_appInfo->info.allocLogSize;
      int someSizeLessThanMin = 0;
      for (int i = 0; i < nbThreads; ++i) {
        volatile long start = __atomic_load_n(&logPtrs[i].log_ptrs.write_log_start, __ATOMIC_ACQUIRE);
        long next = __atomic_load_n(&logPtrs[i].log_ptrs.write_log_next, __ATOMIC_ACQUIRE);
        volatile long size = next >= start ? next - start : totSize - (start - next);
        if (size < MIN_SPACE_IN_LOG) {
          someSizeLessThanMin = 1;
          break;
        }
      }
      if (someSizeLessThanMin) {
        // wait the background replayer to gather some transactions
        continue;
      }
    }

    // MEASURE_TS(tsNext0);
    if ((flags & LOG_REPLAY_ASYNC_SORTER)) {
      tx = replay_log_nextTx_queued();
    } else if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED)) {
      // replay_log_next_entry_s test_tx = replay_log_nextTx_forward_search_pc_sorted(0, 0);
      tx = replay_log_nextTx_forward_search_pc_sorted(1, 1);      
    } else if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD)) {
      // replay_log_next_entry_s tx_test = replay_log_nextTx_backward_search_pc(0);
      tx = replay_log_nextTx_backward_search_pc_sorted(1, 1);      
    } else if ((flags & LOG_REPLAY_FORWARD) && (flags & LOG_REPLAY_LOGICAL_CLOCKS)) {
      tx = replay_log_nextTx_forward_search_lc();
    } else if ((flags & LOG_REPLAY_FORWARD) && (flags & LOG_REPLAY_PHYSICAL_CLOCKS)) {
      tx = replay_log_nextTx_forward_search_pc();
    } else if ((flags & LOG_REPLAY_BACKWARD) && (flags & LOG_REPLAY_LOGICAL_CLOCKS)) {
      tx = replay_log_nextTx_backward_search_lc();
    } else if ((flags & LOG_REPLAY_BACKWARD) && (flags & LOG_REPLAY_PHYSICAL_CLOCKS)) {
      tx = replay_log_nextTx_backward_search_pc(1);
    } else {
      tx = (replay_log_next_entry_s){-1, -1};
    }

    if ((flags & LOG_REPLAY_CONCURRENT) && (tx.idx == -1) && isExit/* && !isRepeat*/) {
      int safeExit = 1;
      for (int i = 0; i < nbThreads; ++i) {
        if (__atomic_load_n(&logPtrs[i].log_ptrs.write_log_next, __ATOMIC_ACQUIRE)
            != __atomic_load_n(&logPtrs[i].log_ptrs.write_log_start, __ATOMIC_ACQUIRE)) {
          safeExit = 0;
          break;
        }
      }
      if ((flags & LOG_REPLAY_BACKWARD)) {
        replay_log_backward_find_minimum_target_ts();
      }
      // isRepeat = 1;
      if (!safeExit) continue;
    }

    if ((flags & LOG_REPLAY_CONCURRENT) && (tx.idx == -1) && !isExit/* && !isRepeat*/) {
      if ((flags & LOG_REPLAY_FORWARD)) {
        replay_log_forward_find_minimum_target_ts();
      }
      // isRepeat = 1;
      continue;
    }

    // isRepeat = 0;

    concWBINVD = countWrites - snapWBINVD;
    if ((flags & LOG_REPLAY_CONCURRENT) && (flags & LOG_REPLAY_FORWARD) && concWBINVD > writesThreshold) {
      snapWBINVD = countWrites;
      // A_MEASURE_TS(tsBufferEnd0); // TODO: put in other TS
      for (int i = 0; i < nbThreads; ++i) {
        // TODO: this must be atomic
        __atomic_store_n(&logPtrs[i].log_ptrs.write_log_start, curPtrs[i], __ATOMIC_RELEASE);
      }
      buffer_end(); // in some solution they flush here
      // A_MEASURE_TS(tsBufferEnd1);
      // A_INC_PERFORMANCE_COUNTER(tsBufferEnd0, tsBufferEnd1, bufferEndTime);
    }    

    if ((flags & LOG_REPLAY_CONCURRENT) && (tx.idx == -1) && !isExit) {
      // TODO: may cause aborts
      if ((flags & LOG_REPLAY_BACKWARD)) {
        for (int i = 0; i < nbThreads; ++i) {
          logPtrs[i].log_ptrs.write_log_start = snapshotPtrs[i];
        }
        replay_log_backward_find_minimum_target_ts();
        continue;
      }
    }
    if (tx.idx == -1) {
      // MEASURE_TS(tsNext1); //unused
      // INC_PERFORMANCE_COUNTER(tsNext0, tsNext1, sorterTime); //unused
      break;
    }

    if (replayerId == 0) {
      // A_MEASURE_INC(stats_nbTxs);
    }
    // MEASURE_TS(tsNext1); //unused
    // INC_PERFORMANCE_COUNTER(tsNext0, tsNext1, sorterTime); //unused

    // MEASURE_TS(tsApply0);
    if ((flags & LOG_REPLAY_BACKWARD)) {
      /*test_curTs = */replay_log_applyTx_backward(tx, replayerId);
    } else if ((flags & LOG_REPLAY_BUFFER_FLUSHES)
        || (flags & LOG_REPLAY_BUFFER_WBINVD)
        || (flags & LOG_REPLAY_RANGE_FLUSHES)) {
      /*test_curTs = */replay_log_applyTx_buffer(tx, replayerId);
    } else if ((flags & LOG_REPLAY_FORWARD)) {
      /*test_curTs = */replay_log_applyTx_andAdvance(tx, replayerId);
    } else {
      tx = (replay_log_next_entry_s){-1, -1};
    }

    // if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED) || (flags & LOG_REPLAY_FORWARD)) {
    //   if (test_curTs < test_lastTs) {
    //     printf("inverted TS found!\n");
    //   }
    //   test_lastTs = test_curTs;
    // } else if ((flags & LOG_REPLAY_BACKWARD)) {
    //   if (test_curTs > test_lastTs) {
    //     printf("inverted TS found!\n");
    //   }
    //   test_lastTs = test_curTs;
    // }

    // MEASURE_TS(tsApply1); //unused
    // INC_PERFORMANCE_COUNTER(tsNext1, tsApply1, applyTime); //unused
  } while (1);

  // A_MEASURE_TS(tsBufferEnd0); // TODO: put in other TS //unused
  buffer_end(); // in some solution they flush here
  // A_MEASURE_TS(tsBufferEnd1); //unused
  // A_INC_PERFORMANCE_COUNTER(tsBufferEnd0, tsBufferEnd1, bufferEndTime); //unused

  // A_MEASURE_TS(ts1);
  // A_INC_PERFORMANCE_COUNTER(ts0, ts1, tempTotalTime);

  // fclose(log_replay_print_file);

  // __sync_add_and_fetch(&stats_timeTotal, tempTotalTime / (nbReplayers < 1 ? 1 : nbReplayers));
  // __sync_add_and_fetch(&stats_timeLookup, sorterTime);
  // __sync_add_and_fetch(&stats_timeApply, applyTime);
  // __sync_add_and_fetch(&stats_timeBufferEnd, bufferEndTime);
  // __sync_add_and_fetch(&stats_countWrites, countWrites);
  // __sync_add_and_fetch(&stats_analizedWrites, analizeWrites);
  // __sync_add_and_fetch(&stats_allWrites, allWrites);

#ifdef GUY_ENABLE_REPLAYER_PRINT_OUT
  printf("[%i] wrote %lu (all=%lu, analized=%lu)\n", replayerId,
    countWrites, allWrites, analizeWrites);
#endif
}

extern "C" {
void* internal_replay_log_apply_fn(void* replayerId)
{
  int arg = (int)((uintptr_t)replayerId);

#ifdef GUY_ENABLE_REPLAYER_PRINT_OUT
  printf("replayer %i waiting for flag replay_is_ready\n", arg);
#endif
  // asm volatile ("" : : : "memory");

  while (!__atomic_load_n(&replay_is_ready, __ATOMIC_ACQUIRE)) usleep(10);  

  snapshotPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curPtrs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  curPtrsThrs[arg] = curPtrs;
  curTSs = (uint64_t*)malloc(nbThreads * sizeof(uint64_t));
  memset(snapshotPtrs, 0, nbThreads * sizeof(uint64_t));
  memset(curPtrs, 0, nbThreads * sizeof(uint64_t)); // TODO: assumes startPtr = 0

  __sync_synchronize();

  internal_replay_log_apply(arg);

#ifdef GUY_ENABLE_REPLAYER_PRINT_OUT
  printf("replayer %i done\n", arg);
#endif
  return NULL;
}
}

void replay_log_apply() /* may not buffer flushes */
{
  __atomic_store_n(&replay_is_ready, 1, __ATOMIC_RELEASE);
  // printf("Checkpointer set replay_is_ready flag = 1\n");
  internal_replay_log_apply(0);
}

// TODO: print data into a file...

static inline replay_log_next_entry_s replay_log_nextTx_queued()
{
  replay_log_next_entry_s *res;
  while (__atomic_load_n(&sorter_buffer_prod_idx, __ATOMIC_ACQUIRE) == sorter_buffer_cons_idx);
    //printf("Consumer waiting async thread!!!\n");
  res = &sorter_buffer[sorter_buffer_cons_idx % sorter_buffer_size];
  __atomic_store_n(&sorter_buffer_cons_idx, sorter_buffer_cons_idx + 1, __ATOMIC_RELEASE);
  // prod_cons_consume(sorterQueue, (void**)&res);
  if (res == (void*)-1) {
    return (replay_log_next_entry_s){-1, -1};
  }
  return *res;
}

static inline replay_log_next_entry_s replay_log_nextTx_forward_search_pc()
{
  uint64_t maxTs = 0;
  int maxThread = -1;
  int foundNone = 0;
  replay_log_next_entry_s res = (replay_log_next_entry_s){curThread, (long)curPtrs[curThread]};

  if (res.threadId == -1) {
    replay_log_forward_find_minimum_target_ts();
    res = (replay_log_next_entry_s){curThread, (long)curPtrs[curThread]};
    if (res.threadId == -1) {
      return (replay_log_next_entry_s){-1, -1};
    }
  }

  if (res.idx == logPtrs[res.threadId].log_ptrs.write_log_next) {
    return (replay_log_next_entry_s){-1, -1};
  }
  if ((flags & LOG_REPLAY_CONCURRENT) && curTSs[curThread] != (uint64_t)-1L && (curTSs[curThread] > *latestSafeTsv)) {
    return (replay_log_next_entry_s){-1, -1};
  }

  long start = curPtrs[curThread];
  while (start != logPtrs[curThread].log_ptrs.write_log_next
      && !isBit63One(writeLog[curThread][start])) {
     start = (start + 2) & (logSize - 1); // <addr, val>
  }
  if (start != logPtrs[curThread].log_ptrs.write_log_next) {
    start = (start + 1) & (logSize - 1); // <ts>
    curPtrs[curThread] = start;    
    if (start != logPtrs[curThread].log_ptrs.write_log_next) {
      // updates TS of new current TX on curThread
      while (start != logPtrs[curThread].log_ptrs.write_log_next && !isBit63One(writeLog[curThread][start])) {
        start = (start + 2) & (logSize - 1); // <addr, val>
      }
      curTSs[curThread] = zeroBit63(writeLog[curThread][start]);
    } else {
      curTSs[curThread] = (uint64_t)-1L;
    }
  }

  // find the next thread
  for (int i = 0; i < nbThreads; ++i) {
    if (curTSs[i] == 0 || curTSs[i] == (uint64_t)-1L) {
      foundNone++;
      continue;
    }
    if (maxThread == -1 || LARGER_THAN(maxTs, curTSs[i], maxThread, i)) {
      maxThread = i;
      maxTs = curTSs[i];
    }
  }
  curThread = maxThread;
  if (foundNone == nbThreads) { // no more log
    return (replay_log_next_entry_s){-1, -1};
  }
  return res;
}

static inline replay_log_next_entry_s replay_log_nextTx_forward_search_lc()
{
  uint64_t target = curTs + 1;
  int foundNone = 0;
  replay_log_next_entry_s res = (replay_log_next_entry_s){curThread, (long)curPtrs[curThread]};

  if (res.idx == logPtrs[res.threadId].log_ptrs.write_log_next) {
    return (replay_log_next_entry_s){-1, -1};
  }

  long start = curPtrs[curThread];
  while (start != logPtrs[curThread].log_ptrs.write_log_next && !isBit63One(writeLog[curThread][start])) {
     start = (start + 2) & (logSize - 1); // <addr, val>
  }
  if (start != logPtrs[curThread].log_ptrs.write_log_next) {
    start = (start + 1) & (logSize - 1); // <ts>
    curPtrs[curThread] = start;
    if (start != logPtrs[curThread].log_ptrs.write_log_next) {
      while (start != logPtrs[curThread].log_ptrs.write_log_next
          && !isBit63One(writeLog[curThread][start])) {
        start = (start + 2) & (logSize - 1); // <addr, val>
      }
      curTSs[curThread] = zeroBit63(writeLog[curThread][start]);
    } else {
      curTSs[curThread] = 0;
    }
  }

  // find the next thread
  for (int i = 0; i < nbThreads; ++i) {
    if (curTSs[i] == 0) {
      foundNone++;
      continue;
    }
    if (curTSs[i] == target) {
      curTs = target;
      curThread = i;
      return res;
    }
  }
  if (foundNone == nbThreads) { // no more log
    return (replay_log_next_entry_s){-1, -1};
  }
  fprintf(stderr, "[ERROR] target ts %lu not found\n", target);
  return (replay_log_next_entry_s){-1, -1};
}

static inline replay_log_next_entry_s replay_log_nextTx_backward_search_lc()
{
  uint64_t target = curTs - 1;
  int foundNone = 0;
  uint64_t ts;
  replay_log_next_entry_s res = (replay_log_next_entry_s){curThread, (long)curPtrs[curThread]};

  if (res.idx == logPtrs[res.threadId].log_ptrs.write_log_start) {
    return (replay_log_next_entry_s){-1, -1};
  }

  long end = curPtrs[curThread];
  while ((int)((end + logSize - 1) & (logSize - 1)) != logPtrs[curThread].log_ptrs.write_log_start && !isBit63One(writeLog[curThread][end])) {
     end = (end + logSize - 2) & (logSize - 1); // <addr, val>
  }
  if ((int)((end + logSize - 1) & (logSize - 1)) != logPtrs[curThread].log_ptrs.write_log_start) {
    ts = zeroBit63(writeLog[curThread][end]);
    curPtrs[curThread] = (end + logSize - 1) & (logSize - 1);
    curTSs[curThread] = ts;
  } else {
    curPtrs[curThread] = end;
    curTSs[curThread] = 0;
  }

  // find the next thread
  for (int i = 0; i < nbThreads; ++i) {
    if (curTSs[i] == 0) {
      foundNone++;
      continue;
    }
    if (curTSs[i] == target) {
      curTs = target;
      curThread = i;
      return res;
    }
  }
  if (foundNone == nbThreads) { // no more log
    return (replay_log_next_entry_s){-1, -1};
  }
  fprintf(stderr, "[ERROR] target ts %lu not found\n", target);
  return (replay_log_next_entry_s){-1, -1};
}

static inline replay_log_next_entry_s replay_log_nextTx_backward_search_pc(int update)
{
  uint64_t maxTs = 0;
  int maxThread = -1;
  int foundNone = 0;
  uint64_t ts;
  replay_log_next_entry_s res = (replay_log_next_entry_s){curThread, (long)curPtrs[curThread]};

  if (res.idx == logPtrs[res.threadId].log_ptrs.write_log_start) {
    return (replay_log_next_entry_s){-1, -1};
  }

  long end = curPtrs[curThread];
  while ((int)((end + logSize - 1) & (logSize - 1)) != logPtrs[curThread].log_ptrs.write_log_start && !isBit63One(writeLog[curThread][end])) {
    end = (end + logSize - 2) & (logSize - 1); // <addr, val>
  }
  if (update) {
    if ((int)((end + logSize - 1) & (logSize - 1)) != logPtrs[curThread].log_ptrs.write_log_start) {
      ts = zeroBit63(writeLog[curThread][end]);
      curPtrs[curThread] = (end + logSize - 1) & (logSize - 1);
      curTSs[curThread] = ts;
    } else {
      curPtrs[curThread] = end;
      curTSs[curThread] = 0;
    }
  }

  // find the next thread
  for (int i = 0; i < nbThreads; ++i) {
    if (curTSs[i] == 0) {
      foundNone++;
      continue;
    }
    if (maxThread == -1 || LARGER_THAN(curTSs[i], maxTs, i, maxThread)) {
      maxThread = i;
      maxTs = curTSs[i];
    }
  }
  curThread = maxThread;
  if (foundNone == nbThreads) { // no more log
    return (replay_log_next_entry_s){-1, -1};
  }
  return res;
}

static inline void replay_log_backward_find_maximum_target_ts()
{
  // TODO: add the TS
  curTs = 0;

  for (int i = 0; i < nbThreads; ++i) {
    long end = logPtrs[i].log_ptrs.write_log_next;
    if (curThread == -1) curThread = i;
    if (end == logPtrs[i].log_ptrs.write_log_start) {
      curTSs[i] = 0;
    }
    end = (end + logSize - 1) & (logSize - 1); // <ts> if no crash
    curPtrs[i] = (end + logSize - 1) & (logSize - 1); // last entry in TX    
    // while (!isBit63One(writeLog[i][end]) && end != logPtrs[i].log_ptrs.write_log_start) {
    //   end = (end + logSize - 1) & (logSize - 1);
    // }
    uint64_t ts;

    if (flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD) {
      end = (end + logSize - 1) & (logSize - 1); // last <val> if no crash
      while (end != logPtrs[curThread].log_ptrs.write_log_start && !isBit63One(writeLog[curThread][end])) {
        // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
        end = (end + logSize - 2) & (logSize - 1); // <addr, val>
      }
    }

    ts = zeroBit63(writeLog[i][end]);
    curTSs[i] = ts;

    if (LARGER_THAN(ts, curTs, i, curThread)) {
      curTs = ts;
      curThread = i;
    }
  }
}

static inline void replay_log_backward_find_minimum_target_ts()
{
  // TODO: add the TS
  curTs = (uint64_t)-1;

  for (int i = 0; i < nbThreads; ++i) {
    snapshotPtrs[i] = curPtrs[i];
    // snapshotPtrs[i] = logPtrs[i].log_ptrs.write_log_start;

    int32_t end = __atomic_load_n(&logPtrs[i].log_ptrs.write_log_next, __ATOMIC_ACQUIRE);
    if (curThread == -1) curThread = i;
    if (end == logPtrs[i].log_ptrs.write_log_start) {
      // log is empty
      
      curTSs[i] = (uint64_t)-1; // will not pick this
      curPtrs[i] = end;
    } else {
      // log is NOT empty

      end = (end + logSize - 1) & (logSize - 1); // <ts> if no crash
      curPtrs[i] = (end + logSize - 1) & (logSize - 1); // last entry in TX
      // while (!isBit63One(writeLog[i][end]) && end != logPtrs[i].log_ptrs.write_log_start) {
      //   end = (end + logSize - 1) & (logSize - 1);
      // }
      uint64_t ts;

      if (flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD) {
        end = (end + logSize - 1) & (logSize - 1); // last <val> if no crash
        while (end != logPtrs[curThread].log_ptrs.write_log_start
            && !isBit63One(writeLog[curThread][end])) {
          // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
          end = (end + logSize - 2) & (logSize - 1); // <addr, val>
        }
      }
      ts = zeroBit63(writeLog[i][end]);
      curTSs[i] = ts;

      if (LARGER_THAN(curTs, ts, curThread, i)) {
        curTs = ts;
        curThread = i;
      }
    }
  }

  snapshotPtrs[curThread] = curPtrs[curThread];
}

static inline void replay_log_forward_find_minimum_target_ts()
{
  curTs = (uint64_t)-1;
  for (int i = 0; i < nbThreads; ++i) {
    // with concurrent multi replayers we need to check if curPtrs is ahead 
    // long start = curPtrs[i]; // logPtrs[i].log_ptrs.write_log_start;
    long start = logPtrs[i].log_ptrs.write_log_start;
    volatile int end = __atomic_load_n(&logPtrs[i].log_ptrs.write_log_next, __ATOMIC_ACQUIRE);
    if (curThread == -1) curThread = i;
    if (start == end) {
      curPtrs[i] = start;
      curTSs[i] = (uint64_t)-1;
    } else {
      curPtrs[i] = start;      
      while (!isBit63One(__atomic_load_n(&writeLog[i][start], __ATOMIC_ACQUIRE)) && start != end) {
        start = (start + 2) & (logSize - 1);
        end = __atomic_load_n(&logPtrs[i].log_ptrs.write_log_next, __ATOMIC_ACQUIRE);
      }      
      uint64_t ts = zeroBit63(writeLog[i][start]);
      curTSs[i] = ts;
      if (LARGER_THAN(curTs, ts, curThread, i)) {
        curTs = ts;
        curThread = i;
      }
    }
  }
}

static inline replay_log_next_entry_s replay_log_nextTx_forward_search_pc_sorted(int useHints, int updateCurTx)
{
  if (curTx.threadId == -1 || !useHints) { // first TX, must lookup
    if ((flags & LOG_REPLAY_CONCURRENT)) {
      replay_log_forward_find_minimum_target_ts();
    }
    int minTsThread = -1;
    int minTsIdx = -1;
    int minTsStartIdx = -1;
    int nextIdx = -1;
    int minTsNextIdx = -1;
    uint64_t minTs = (uint64_t)-1;
    // printf("curTx.threadId is -1\n");
    for (int i = 0; i < nbThreads; ++i) {
      //// ----
      long start = curPtrs[i];
      if (start == logPtrs[i].log_ptrs.write_log_next) continue;
      nextIdx = start;
      start = (start + 1) & (logSize - 1); // <nextTXInLog>
      int saveStart = start;
      // 
      while (!isBit63One(writeLog[i][start])) {
        // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
        start = (start + 2) & (logSize - 1); // <addr, val>
        if (start == logPtrs[i].log_ptrs.write_log_next) break;
      }
      uint64_t obsTs = zeroBit63(writeLog[i][start]);
      if (start == logPtrs[i].log_ptrs.write_log_next) continue;
      // minTs >= obsTs
      if (minTs == (uint64_t)-1 || LARGER_THAN(minTs, obsTs, minTsThread, i)) {
        minTs = obsTs;
        minTsThread = i;
        minTsStartIdx = saveStart;
        minTsNextIdx = nextIdx;
        minTsIdx = (start + 1) & (logSize - 1); // position with info of next TX
      }
      //// ----
    }
    if (minTs != (uint64_t)-1 && minTsIdx != logPtrs[minTsThread].log_ptrs.write_log_next) {
      // if (updateCurTx) curPtrs[minTsThread] = minTsIdx;
      if (updateCurTx && isBit63One(writeLog[minTsThread][minTsNextIdx])) {
        uint64_t nextTxInfo = zeroBit63(writeLog[minTsThread][minTsNextIdx]);
        curTx.threadId = nextTxInfo >> 32;
        curTx.idx = nextTxInfo & 0xFFFFFFFF;
      }
      // printf("    found on log=%i idx=%i\n", minTsThread, minTsStartIdx);
      return (replay_log_next_entry_s){minTsThread, minTsStartIdx};
    }
  } else { // we know the next thread
    int startThread = curTx.threadId;
    long startIdx = curTx.idx;
    replay_log_next_entry_s res = (replay_log_next_entry_s){startThread, (int)((startIdx + 1) & (logSize - 1))};

    if (!isBit63One(writeLog[startThread][startIdx])) {
      curTx = (replay_log_next_entry_s){-1, -1};
      // --> updated by the applier (TODO)
      // printf("%d\n", curTx.threadId);
    } else {
      uint64_t nextTxInfo = zeroBit63(writeLog[startThread][startIdx]);
      curTx.threadId = nextTxInfo >> 32;
      curTx.idx = nextTxInfo & 0xFFFFFFFF;
    }
    return res;
  }
  return (replay_log_next_entry_s){-1, -1};
}

static inline replay_log_next_entry_s replay_log_nextTx_backward_search_pc_sorted(int useHints, int updateCurTx)
{
  if (curTx.threadId == -1 || !useHints) { // first TX, must lookup
    int minTsThread = -1;
    int minTsEndIdx = -1;
    uint64_t minTs = 0;
    for (int i = 0; i < nbThreads; ++i) {
      long end = curPtrs[i];
      uint64_t obsTs = curTSs[i];
      if (end == logPtrs[i].log_ptrs.write_log_start) {
        continue;
      }
      // printf("passou aqui!!!!\n");
      if (LARGER_THAN(obsTs, minTs, i, minTsThread)) {
        minTs = obsTs;
        minTsThread = i;
        minTsEndIdx = end;
      }
    }
    if (minTsEndIdx != -1 && minTsEndIdx != logPtrs[minTsThread].log_ptrs.write_log_start) {
      uint64_t end = (minTsEndIdx + 1) & (logSize - 1); // TODO: bug
      uint64_t linkRec = writeLog[minTsThread][end];
      if (!isBit63One(linkRec)) {
        // bug on the begining of the log
        minTsEndIdx = (minTsEndIdx + 1) & (logSize - 1);
      }
      if (updateCurTx && isBit63One(linkRec)) {
        uint64_t nextTxInfo = zeroBit63(writeLog[minTsThread][end]);
        curTx.threadId = nextTxInfo >> 32;
        curTx.idx = nextTxInfo & 0xFFFFFFFF;
      }
      return (replay_log_next_entry_s){minTsThread, minTsEndIdx};
    }
  } else { // we know the next thread
    int logId = curTx.threadId;
    int startIdx = curTx.idx;
    int endIdx = startIdx;

    // printf("indexed curTx.idx = %i (log: %lx)\n", endIdx, writeLog[endThread][endIdx]);

      // printf("passou aqui!!!!\n");
    endIdx = (endIdx + 1) & (logSize - 1);
    while (!isBit63One(writeLog[logId][endIdx])) {
      endIdx = (endIdx + 2) & (logSize - 1);
    }
    uint64_t nextTxInfoRec = writeLog[logId][endIdx];
    if (nextTxInfoRec == (uint64_t)-1) {
      curTx = (replay_log_next_entry_s){-1, -1};
      return (replay_log_next_entry_s){logId, endIdx};
    }
    uint64_t nextTxInfo = zeroBit63(nextTxInfoRec);

    curTx.threadId = nextTxInfo >> 32;
    curTx.idx = nextTxInfo & 0xFFFFFFFF;
    // printf("next TX is log=%2i (cur=%2i) idx=%9i (cur=%9i)\n", curTx.threadId, logId, curTx.idx, endIdx);

    // printf(" --- before curTx.idx = %i (log: %lx)\n", endIdx, writeLog[curTx.threadId][endIdx]);
    // endIdx = (endIdx + 1) & (logSize - 1);
    endIdx = (endIdx + logSize - 1) & (logSize - 1);
    // printf(" --- next curTx.idx = %i (log: %lx, end log: %li, resIdx: %i)\n", endIdx,
    //   writeLog[curTx.threadId][endIdx], logPtrs[curTx.threadId].log_ptrs.write_log_next, endIdx);

    return (replay_log_next_entry_s){logId, endIdx};
  }
  return (replay_log_next_entry_s){-1, -1};
}

static inline int check_apply(uintptr_t addr, int replayerId)
{
  int numaSocket = replayerId & 1;

  if ((nbReplayers <= 1 || !(flags & LOG_REPLAY_PARALLEL))
     && (
       (addr >= (uintptr_t)nvramRanges[0] && addr < (uintptr_t)nvramRanges[1]) // socket 0
       ||
       (addr >= (uintptr_t)nvramRanges[2] && addr < (uintptr_t)nvramRanges[3]) // socket 1
     ) // TODO: also checks for range (if address not in PM should rise an exception)
    ) return 1;
  // printf("[%i] range0 (%p - %p) [%i], range1 (%p - %p) [%i]  addr = %p mod = %i cond = %i\n",
  //   replayerId,
  //   nvramRanges[0], nvramRanges[1], addr >= (uintptr_t)nvramRanges[0] && addr < (uintptr_t)nvramRanges[1],
  //   nvramRanges[2], nvramRanges[3], addr >= (uintptr_t)nvramRanges[2] && addr < (uintptr_t)nvramRanges[3],
  //   addr,
  //   (addr >> 12) % (nbReplayers / 2),
  //   (addr >> 12) % (nbReplayers / 2) != (uintptr_t)(replayerId / 2));
  // if ((nbReplayers % 2)) {

  // // word
  // if (((addr >> 3) & ((nbReplayers >> 1)-1)) == (uintptr_t)(replayerId >> 1)) {
  // // cache-line
  // if (((addr >> 6) & ((nbReplayers >> 1)-1)) == (uintptr_t)(replayerId >> 1)) {
  // // optane-granule
  // if (((addr >> 8) & ((nbReplayers >> 1)-1)) == (uintptr_t)(replayerId >> 1)) {
  // memory-page
  if (((addr >> 12) & ((nbReplayers >> 1)-1)) == (uintptr_t)(replayerId >> 1)) {
    return ((addr >= (uintptr_t)nvramRanges[numaSocket*2]) && (addr < (uintptr_t)nvramRanges[numaSocket*2+1]));
  }
  return 0;
}

static inline uint64_t replay_log_applyTx_andAdvance(replay_log_next_entry_s curPtr, int replayerId)
{
  uint64_t res = -1;
  long idx = curPtr.idx;
  while (!isBit63One(writeLog[curPtr.threadId][idx])) {
    // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
    uintptr_t addr = writeLog[curPtr.threadId][idx];
    uintptr_t val = writeLog[curPtr.threadId][(idx + 1) & (logSize - 1)];
    idx = (idx + 2) & (logSize - 1); // <addr, val>

    allWrites++;

    if (!check_apply(addr, replayerId)) continue;

    analizeWrites++; // compare this with size of log

    countWrites++;
    *((uintptr_t*)addr) = val;

    if (idx == logPtrs[curPtr.threadId].log_ptrs.write_log_next) break;
    FLUSH_CL((void*)addr); // TODO: coalesce repeated cachelines
  }
  if (idx != logPtrs[curPtr.threadId].log_ptrs.write_log_next) {
    res = zeroBit63(writeLog[curPtr.threadId][idx]);
    // printf("REP TX=%lx\n", res);
  }
  // TODO: check if we can do this for the other solutions
  if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED)) {
    curPtrs[curPtr.threadId] = (idx + 1) & (logSize - 1); // <commit marker>    
  }
  if ((flags & LOG_REPLAY_CONCURRENT)) {
    for (int i = 0; i < nbThreads; i++) {
      if ((flags & LOG_REPLAY_CONCURRENT) && (curTSs[i] == (uint64_t)-1L)) {
        replay_log_forward_find_minimum_target_ts();
        break;
      }
    }
  }
  // idx = (idx + 1) & (logSize - 1); // <commit marker>
  FENCE_PREV_FLUSHES(); // call sfence
  return res;
}

static inline uint64_t replay_log_applyTx_buffer(replay_log_next_entry_s curPtr, int replayerId)
{
  uint64_t res = -1;
  long idx = curPtr.idx;
  while (!isBit63One(writeLog[curPtr.threadId][idx])) {
    // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
    uintptr_t addr = writeLog[curPtr.threadId][idx];
    uintptr_t val = writeLog[curPtr.threadId][(idx + 1) & (logSize - 1)];
    idx = (idx + 2) & (logSize - 1); // <addr, val>

    allWrites++;
    /*volatile */int isToApply = check_apply(addr, replayerId);
    if (!isToApply) continue;

    analizeWrites++; // compare this with size of log
  
    countWrites++;
    *((uintptr_t*)addr) = val;

    if (idx == logPtrs[curPtr.threadId].log_ptrs.write_log_next) break;
    if (flags & LOG_REPLAY_BUFFER_FLUSHES) {
      // uint64_t ts0, ts1; //unused
      // MEASURE_TS(ts0); //unused
      dirty_cache_lines.insert(addr & CACHE_LINE_MASK);
      // MEASURE_TS(ts1); //unused
      // INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeSetInsert); //unused
    }
  }
  if (idx != logPtrs[curPtr.threadId].log_ptrs.write_log_next) {
    res = zeroBit63(writeLog[curPtr.threadId][idx]);
  }

  // TODO: check if we can do this for the other solutions
  if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED)) {    
    curPtrs[curPtr.threadId] = (idx + 1) & (logSize - 1); // <commit marker>    
  }

  if ((flags & LOG_REPLAY_CONCURRENT) && replayerId == 0) {
    __atomic_store_n(&logPtrs[curPtr.threadId].log_ptrs.write_log_start, (idx + 1) & (logSize - 1), __ATOMIC_RELEASE); // <commit marker>
  }
  if ((flags & LOG_REPLAY_CONCURRENT)) {
    for (int i = 0; i < nbThreads; i++) {
      if ((flags & LOG_REPLAY_CONCURRENT) && (curTSs[i] == (uint64_t)-1L)) {
        replay_log_forward_find_minimum_target_ts();
        break;
      }
    }
  }

  // idx = (idx + 1) & (logSize - 1); // <commit marker>
  return res;
}

// int test_ptrs[128];

static inline uint64_t replay_log_applyTx_backward(replay_log_next_entry_s curPtr, int replayerId)
{
  uint64_t res = -1;
  long idx = curPtr.idx;
  if (snapshotPtrs[curPtr.threadId] == (uint64_t)logPtrs[curPtr.threadId].log_ptrs.write_log_start) {
    snapshotPtrs[curPtr.threadId] = (idx + 2) & (logSize - 1); // after the <ts>
  }
  // idx = (idx + logSize - 2) & (logSize - 1); // <next, TS>
  while (!isBit63One(writeLog[curPtr.threadId][idx])) { // looks for the other TS (beginning of TX)
    // passes all the writes ands goes to the TSs (assumes <addr,val>, first bit of addrs is 0)
    uintptr_t val = writeLog[curPtr.threadId][idx];
    uintptr_t addr = writeLog[curPtr.threadId][(idx + logSize - 1) & (logSize - 1)];

    // check if it is still an addr or is the start of the log
    idx = (idx + logSize - 2) & (logSize - 1); // <addr, val>
    // if (addr == 0x0/*NULL*/) continue; // BUG
    if ((int)idx == (int)logPtrs[curPtr.threadId].log_ptrs.write_log_start) break; // for the linking
    if ((int)((idx + 1) & (logSize - 1)) == (int)logPtrs[curPtr.threadId].log_ptrs.write_log_start) break;

    allWrites++;

    if (!check_apply(addr, replayerId)) {
      continue;
    }
    // TODO: may be filtered
    analizeWrites++; // compare this with size of log

    // if (curTs == 1) {
    //   printf("Applying %p <- %lu (idx = %i)\n", (void*)addr, val, idx);
    // }
    // uint64_t ts0, ts1; //unused

    if (!BITSET_IS_SET(addr)) {
      // MEASURE_TS(ts0); //unused
      countWrites++;
      BITSET_SET(addr);
      *((uintptr_t*)addr) = val;
      // MEASURE_TS(ts1); //unused
      // INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeMapInsert); //unused
    }

    // TODO: create a bitset per range
    // int inRange = 0;
    // for (int i = 0; i < nbNvramRanges; ++i) {
    //   if (addr >= (uintptr_t)nvramRanges[i*2] && addr < (uintptr_t)nvramRanges[i*2+1]) {
    //     inRange = 1;
    //   }
    // }
    // if (!inRange) {
    //   printf("Not in range! nbNvramRanges=%i \n   %p-%p | %p-%p \n   addr=%p\n", nbNvramRanges,
    //     nvramRanges[0], nvramRanges[1], nvramRanges[2], nvramRanges[3], addr);
    // }

    // -------------- TODO: drop the map
    // MEASURE_TS(ts0);
    // uintptr_t cl_addr = addr & CACHE_LINE_MASK;
    // uint8_t in_cl_addr = 1 << ((addr >> 3) & 0b111);
    // auto findWritten = written_cache_lines.find(cl_addr);
    // if (findWritten != written_cache_lines.end()) {
    //   if ((findWritten->second & in_cl_addr) == 0) {
    //     findWritten->second |= in_cl_addr;
    //     MEASURE_TS(ts1);
    //     INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeMapInsert);
    //     countWrites++;
    //     *((uintptr_t*)addr) = val;
    //     // printf("[%i] found %p but needs to update %p\n", replayerId, (void*)cl_addr, (void*)addr);
    //   } else {
    //     MEASURE_TS(ts1);
    //     INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeMapInsert);
    //   }
    // } else {
    //   written_cache_lines[cl_addr] = in_cl_addr;
    //   MEASURE_TS(ts1);
    //   INC_PERFORMANCE_COUNTER(ts0, ts1, stats_timeMapInsert);
    //   countWrites++;
    //   *((uintptr_t*)addr) = val;
    //   // printf("[%i] added CL=%p for addr %p\n", replayerId, (void*)cl_addr, (void*)addr);
    // }
    // -------------- TODO: drop the map
  }
  if ((flags & LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD)) {

    if (idx == logPtrs[curPtr.threadId].log_ptrs.write_log_start) {
      curPtrs[curPtr.threadId] = logPtrs[curPtr.threadId].log_ptrs.write_log_start;
      // printf("  END applied log=%2i idx=%9i\n", curPtr.threadId, curPtr.idx);
    } else {
      // printf("  ... applied log=%2i idx=%9i\n", curPtr.threadId, curPtr.idx);
      curPtrs[curPtr.threadId] = (idx + logSize - 2) & (logSize - 1); // <commit marker>      curTSs[curPtr.threadId] = zeroBit63(writeLog[curPtr.threadId][(idx + logSize - 1) & (logSize - 1)]); // <commit marker>
    }
  }

  if ((int)((idx + 1) & (logSize - 1)) == (int)logPtrs[curPtr.threadId].log_ptrs.write_log_start
      && idx != logPtrs[curPtr.threadId].log_ptrs.write_log_start) {
    res = zeroBit63(writeLog[curPtr.threadId][idx]);
  }
  // idx = (idx + 1) & (logSize - 1); // <commit marker>
  return res;
}