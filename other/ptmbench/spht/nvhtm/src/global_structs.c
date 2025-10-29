#define _POSIX_C_SOURCE 200112L
#define _XOPEN_SOURCE   600
#define _BSD_SOURCE     1
#define _GNU_SOURCE     1
#define _DEFAULT_SOURCE 1

#include "global_structs.h"
#include "impl.h" /* also includes global_structs */

#include "htm_impl.h"

#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/wait.h>

#include <stdlib.h>
#include <string.h>
#include <signal.h>

#define FORK_CHECKPOINTER

#ifndef FORK_CHECKPOINTER
  pthread_t checkPointerThread;
#endif

#define GUY_SPHT_NUM_REPLAYERS 1

#ifndef MAX_THREAD
// #define MAX_THREAD 64
#define MAX_THREAD 96
#endif

#ifndef NUM_NUMA_NODES
#define NUM_NUMA_NODES 2
#endif

// TODO: just added this for vcode to stop complaining (not needed)
// #include "htm_arch.h"
#ifndef ARCH_CACHE_LINE_SIZE
#define ARCH_CACHE_LINE_SIZE 64
#endif

#define DEV_SHM "/dev/shm/"

#ifndef NVMALLOC_FILE
#define NVMALLOC_FILE "nvmalloc_file"
#endif /* NVMALLOC_FILE */

#ifndef NVMALLOC_THREAD_PRIV_FILE
#define NVMALLOC_THREAD_PRIV_FILE "nvmalloc_file_priv_t"
#endif /* NVMALLOC_THREAD_PRIV_FILE */

#ifndef NVMALLOC_THREAD_SHAR_FILE
#define NVMALLOC_THREAD_SHAR_FILE "nvmalloc_file_shar_t"
#endif /* NVMALLOC_THREAD_SHAR_FILE */

#ifndef NVMALLOC_SIZE
#define NVMALLOC_SIZE 16777216L /* 1048576L - 1MB */
#endif /* NVMALLOC_SIZE */

#ifndef NVMALLOC_THREAD_PRIV_SIZE
#define NVMALLOC_THREAD_PRIV_SIZE 3221225472L /* 1GB */
#endif /* NVMALLOC_THREAD_PRIV_SIZE */

#ifndef NVMALLOC_THREAD_SHAR_SIZE
#define NVMALLOC_THREAD_SHAR_SIZE 1048576L /* 1MB + logs to be defined in init */
#endif /* NVMALLOC_THREAD_SHAR_SIZE */

/*extern*/__thread long nbTransactions = 0;

static uint64_t nvmalloc_count = 0;
static void *nvmalloc0_base_ptr;
static void *nvmalloc0_current_ptr;
static void *nvmalloc1_base_ptr;
static void *nvmalloc1_current_ptr;
static size_t nvmalloc_size = NVMALLOC_SIZE;

static void **nvmalloc_thr_priv_base_ptr;
static void **nvmalloc_thr_shar_base_ptr;
static __thread void *nvmalloc_thr_priv_base_ptr2 = NULL;
static __thread void *nvmalloc_thr_priv_current_ptr = NULL;
static __thread void *nvmalloc_thr_shar_base_ptr2 = NULL;
static __thread void *nvmalloc_thr_shar_current_ptr = NULL;
static long nvmalloc_thr_priv_size = NVMALLOC_THREAD_PRIV_SIZE;
static long nvmalloc_thr_shar_size = NVMALLOC_THREAD_SHAR_SIZE;

#ifdef FORK_CHECKPOINTER
  static int pidChildProc;
#endif

volatile __thread uint64_t timeTotalTS1 = 0;
volatile __thread uint64_t timeAfterTXTS1 = 0;
volatile __thread uint64_t timeTotalTS2 = 0;
volatile __thread uint64_t timeTotal = 0;

__thread uint64_t timeSGL_TS1 = 0;
__thread uint64_t timeSGL_TS2 = 0;
__thread uint64_t timeSGL = 0;
uint64_t timeSGL_global = 0;

__thread uint64_t timeAbortedTX_TS1 = 0;
__thread uint64_t timeAbortedTX_TS2 = 0;
__thread uint64_t timeAbortedTX = 0;
uint64_t timeAbortedTX_global = 0;

volatile __thread uint64_t timeAfterTXSuc = 0;
volatile __thread uint64_t timeAfterTXFail = 0;

static void *nvramRanges[1024];
static int nbNvramRanges;

static const int EPOCH_TIMOUT = 32;

#define MAX_HYPER_THREADS 96

#ifndef PM_DIR
  #ifndef DISABLE_PMEM
    #define PM_DIR_SOCKET0 "/mnt/pmem0_mount/gccoccim/spht/"
    #define PM_DIR_SOCKET1 "/mnt/pmem1_mount/gccoccim/spht/"
  #else
    #define PM_DIR_SOCKET0 "/dev/shm/spht0/"
    #define PM_DIR_SOCKET1 "/dev/shm/spht1/"
  #endif
#endif /* PM_DIR */

// numa_node nvram regions
/* extern */char* NVRAM_REGIONS[] = {PM_DIR_SOCKET0, PM_DIR_SOCKET1};

/* extern */wait_commit_fn_t wait_commit_fn = wait_commit_pcwm2;

// one must call install_bindings_<solution> to set this
/* extern */void(*on_htm_abort)(int threadId);
/* extern */void(*on_before_htm_begin)(int threadId);
/* extern */void(*on_before_htm_write)(int threadId, void *addr, uint64_t val);
/* extern */void(*on_before_htm_commit)(int threadId);
/* extern */void(*on_after_htm_commit)(int threadId);
/*extern */replay_log_next_entry_s(*log_replay_next_tx_search)();
/*extern */uint64_t(*log_replay_next_tx_apply)(replay_log_next_entry_s curPtr);

// 1 cache line per thread to flag the current state
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *gs_ts_array;

/*extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *G_observed_ts;

/*extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  large_cache_line_s *P_last_safe_ts;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **P_epoch_ts;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t *P_epoch_persistent; /* persistent */

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s *G_next;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s *gs_appInfo;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s gs_log_data;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **P_write_log;

/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int P_start_epoch = 0; /* persistent */

/* extern */volatile int *G_epoch_lock;

// TODO: these are also extern
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int32_t *G_flag_checkpointer_exit;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int32_t *G_flag_checkpointer_ready;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  int32_t *G_flag_checkpointer_done;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  cache_line_s **G_flag_checkpointer_G_next;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t ***G_flag_checkpointer_P_write_log;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **G_flag_checkpointer_P_last_safe_ts;
/* extern */volatile __attribute__((aligned(ARCH_CACHE_LINE_SIZE)))
  uint64_t **G_flag_checkpointer_P_last_safe_ts;

/* extern */volatile __thread void(*onBeforeWrite)(int, void*, uint64_t); /* = on_before_htm_write*/
/* extern */volatile __thread void(*onBeforeHtmCommit)(int); /* = on_before_htm_commit */
/* extern */volatile __thread uint64_t *write_log_thread; /* = &(P_write_log[threadId][0]); */

static void *alocateInNVRAM(const char *memRegion, const char *file, size_t bytes, long mapFlag, void *addr)
{
  char fileNameBuffer[1024];
  void *res = NULL;
  int fd;
  
  sprintf(fileNameBuffer, "%s%s", memRegion, file);
  // fd = open(fileNameBuffer, O_CREAT | O_TRUNC | O_RDWR, 0666);
  // close(fd); // writes the permissions
  // fd = open(fileNameBuffer, O_CREAT | O_RDWR, 0666);

  struct stat buf;
  if (stat(fileNameBuffer, &buf) == 0) { //file exists         
    if (remove(fileNameBuffer) != 0) {
        printf("Error removing %s \nExiting\n", fileNameBuffer);
        exit(-1);
    }
    printf("Removed %s\n", fileNameBuffer);
  }

  fd = open(fileNameBuffer, O_CREAT | O_RDWR, 0666);
  printf("Created fresh file: %s\n", fileNameBuffer);

  if (fd == -1) {
    fprintf(stderr, "Error open file %s: %s\n", fileNameBuffer, strerror(errno));
  }

  // TODO: ftruncate needed after munmap...
  if (ftruncate(fd, bytes)) { // if address != NULL there was a ftruncate before
    // fprintf(stderr, "Error ftruncate file %s: %s\n", fileNameBuffer, strerror(errno));
    printf("Error ftruncate file %s\n", fileNameBuffer);
    exit(-1);
  }

  if (addr != NULL) {
    res = mmap(addr, bytes, PROT_READ | PROT_WRITE, mapFlag | MAP_FIXED, fd, 0);
    if (res != addr) {
      // fprintf(stderr, "Error getting the requested address %p (got %p): %s\n", addr, res, strerror(errno));
      printf("Error getting the requested address %p (got %p)\n", addr, res);
      exit(-1);
    }
  } else {
    res = mmap(NULL, bytes, PROT_READ | PROT_WRITE, mapFlag, fd, 0);
  }
  if (res == (void*)-1 || res == NULL) {
    // fprintf(stderr, "Error mmapping file %s: %s\n", fileNameBuffer, strerror(errno));
    printf("Error mmapping file %s\n", fileNameBuffer);
    exit(-1);
  }
  return res;
}

void* internal_replay_log_apply_fn(void* replayerId); // implemented on impl_log_replayer.cpp

static void checkpointer()
{
  printf("checkpointer started\n");
  // TODO: pin this thread to some unused slot
  __atomic_store_n(G_flag_checkpointer_ready, 1, __ATOMIC_RELEASE);
  // printf("checkpointer ready\n");

  //GUY::NOTE forking in setbench causes some issues if your run with the provided python tools
  //GUY:: moving this barrier up so we dont create the replayer threads until we need them
  // this will therefore only execute the checkpointer once we call TM_SHUTDOWN
  // if we want to do this concurrently with worker threads we need to set the flag ourselves
  // Since LOG_REPLAY_CONCURRENT appears to be off by default we will not handle this case
  // it is also unclear how we would fairly compare to SPHT if it requires extra replay threads
  // that run concurrently with worker threads  
  // printf("checkpointer waiting on G_flag_checkpointer_exit\n");
  // while(!__atomic_load_n(G_flag_checkpointer_exit, __ATOMIC_ACQUIRE)) {
  //   pthread_yield();
  //   usleep(100);
  // }
  // printf("checkpointer saw G_flag_checkpointer_exit = 1\n");

  pthread_t threads[gs_appInfo->info.nbReplayers];
  if (log_replay_flags & LOG_REPLAY_PARALLEL) {
    for (int i = 0; i < gs_appInfo->info.nbReplayers-1; i++) {
      if (0 == pthread_create(&(threads[i]), NULL, internal_replay_log_apply_fn, (void*)((uintptr_t)i+1))) {      
        // printf("created replayer thread %d\n", i);
      } 
      else {
        printf("error creating replay thread\n");
        exit(-1);
      }     
    }
  }

  if (log_replay_flags == 0) {
    __atomic_store_n(G_flag_checkpointer_done, 1, __ATOMIC_RELEASE);
    #ifndef FORK_CHECKPOINTER
      return;  
    #else
      exit(EXIT_SUCCESS);
    #endif  
  }
  
  if (!(log_replay_flags & (LOG_REPLAY_CC_HTM|LOG_REPLAY_CONCURRENT))) {
    while(!__atomic_load_n(G_flag_checkpointer_exit, __ATOMIC_ACQUIRE)) {
      pthread_yield();
      usleep(100);
    }
  }
  // after exit replay the log (assumes very long logs)

  //   /* ----------------------------- */
  // // fork again for perf
  // if (fork() == 0) {
  //   char buf[1024];
  //   sprintf(buf, "perf stat -p %d -e L1-dcache-load-misses,L1-dcache-loads,LLC-load-misses,LLC-loads sleep 5 ", pidChildProc);
  //   printf("[PERF] attaching to proc %d\n!", pidChildProc);
  //   execl("/bin/sh", "sh", "-c", buf, NULL);
  //   // done with this code
  //   exit(0);
  // }
  // /* ----------------------------- */

  // TODO: strange bug: the log is not visible on replay_log_init
  while (!(__atomic_load_n(G_flag_checkpointer_P_write_log, __ATOMIC_ACQUIRE))); // must not be NULL!

  replay_log_init(gs_appInfo->info.nbThreads, 
    gs_appInfo->info.nbReplayers, 
    gs_appInfo->info.allocLogSize,
    *G_flag_checkpointer_G_next, 
    *G_flag_checkpointer_P_write_log, 
    *G_flag_checkpointer_P_last_safe_ts,
    log_replay_flags, nvramRanges, nbNvramRanges);

  replay_log_apply();

  if (log_replay_flags & LOG_REPLAY_PARALLEL) {
    printf("Checkpointer waiting for other replayers to finish\n");
    for (int i = 0; i < gs_appInfo->info.nbReplayers-1; i++) {
      pthread_join(threads[i], NULL);
    }
  }


  __atomic_store_n(G_flag_checkpointer_done, 1, __ATOMIC_RELEASE);
  printf("Checkpointer exiting\n");
  #ifndef FORK_CHECKPOINTER
    return;  
  #else
    exit(EXIT_SUCCESS);
  #endif  
}

void handleSIGCHLD(int sigNum) {
  printf("Recieved SIGCHLD %d\n", sigNum);
}

#ifndef FORK_CHECKPOINTER
struct _checkPointerArgs {
  int nbThreads;
  void *addrNUMA0;
  void *addrNUMA1;
};
typedef struct _checkPointerArgs checkPointerArgs_s;
checkPointerArgs_s checkPointerArgs;

void* checkPointerThreadFunc(void* _args) {
  checkPointerArgs_s* args = (checkPointerArgs_s*)(_args);
  int nbThreads = args->nbThreads;
  void *addrNUMA0 = args->addrNUMA0;
  void *addrNUMA1 = args->addrNUMA1;
  char *nvram_regions[] = {PM_DIR_SOCKET0, PM_DIR_SOCKET1};  

  void *prevAddr0 = addrNUMA0;
  void *prevAddr1 = addrNUMA1;

  long poolAreas = nbThreads <= 1 ? 1 : (MAX_THREAD / 2); 

  if (munmap(addrNUMA0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2)) {
    fprintf(stderr, "Error munmap %s: %s\n", NVMALLOC_FILE, strerror(errno));
  }
  if (munmap(addrNUMA1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2)) {
    fprintf(stderr, "Error munmap %s: %s\n", NVMALLOC_FILE, strerror(errno));
  }

  char localMallocFile0[1024];
  char localMallocFile1[1024];
  sprintf(localMallocFile0, "%s%i", NVMALLOC_THREAD_PRIV_FILE, 0);
  sprintf(localMallocFile1, "%s%i", NVMALLOC_THREAD_PRIV_FILE, 1);

  addrNUMA0 = alocateInNVRAM(nvram_regions[0], localMallocFile0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
    /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr0);
  addrNUMA1 = alocateInNVRAM(nvram_regions[1], localMallocFile1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
    /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr1);

  nvmalloc0_base_ptr = ((char*)addrNUMA0) + nvmalloc_thr_priv_size * poolAreas;
  nvmalloc1_base_ptr = ((char*)addrNUMA1) + nvmalloc_thr_priv_size * poolAreas;
  nvmalloc0_current_ptr = nvmalloc0_base_ptr;
  nvmalloc1_current_ptr = nvmalloc1_base_ptr;

  nvramRanges[2*nbNvramRanges] = addrNUMA0;
  nvramRanges[2*nbNvramRanges+1] = (void*)(((uintptr_t)addrNUMA0) + nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2);
  nbNvramRanges++;

  nvramRanges[2*nbNvramRanges] = addrNUMA1;
  nvramRanges[2*nbNvramRanges+1] = (void*)(((uintptr_t)addrNUMA1) + nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2);
  nbNvramRanges++;
  
  checkpointer();
  return NULL;
}
#endif

void *nvmalloc_init(
  int nbThreads,
  uint64_t logBytesPerThread,
  uint64_t sharedBytes,
  char *nvram_regions[]
) {
  printf("Starting nvmalloc_init()\n");

#ifdef DISABLE_PHTM
  printf("PHTM disabled\n");
#else
  printf("PHTM enabled\n");
#endif

#ifdef DISABLE_PMEM
  printf("PMEM disabled\n");
#else
  printf("PMEM enabled\n");
#endif

#ifndef USE_NOOP_FLUSH_FENCE
  printf("USE_NOOP_FLUSH_FENCE disabled\n");
#else
  printf("USE_NOOP_FLUSH_FENCE enabled\n");
#endif

#ifdef FORK_CHECKPOINTER
  printf("FORK_CHECKPOINTER enabled\n");
#else
  printf("FORK_CHECKPOINTER disabled\n");
#endif

  nvmalloc_thr_priv_size = NVMALLOC_THREAD_PRIV_SIZE;
  nvmalloc_thr_shar_size = NVMALLOC_THREAD_SHAR_SIZE;

  // TODO: fork the process here, map shared in the child process (checkpointer), and private in the father
  volatile intptr_t readSharedMem = 0;

  //GUY::TODO is this "./" supposed to be in a NVRAM mount? the comment below does not make sense
  // put this in a non-DAX fs to make use of shared DRAM mappings
  // G_flag_checkpointer_exit = (int*)alocateInNVRAM("./", "flag_checkpointer", 4096 /* var space */, MAP_SHARED, NULL);
  G_flag_checkpointer_exit = (int*)alocateInNVRAM(DEV_SHM, "flag_checkpointer", 4096 /* var space */, MAP_SHARED, NULL);
  char *beginFlags = (char*)G_flag_checkpointer_exit;
  int sizeOfExitFlag = sizeof(__typeof__(G_flag_checkpointer_exit));
  G_flag_checkpointer_ready = (__typeof__(G_flag_checkpointer_ready))(beginFlags + sizeOfExitFlag);
  G_flag_checkpointer_done = (__typeof__(G_flag_checkpointer_done))(beginFlags + 2*sizeOfExitFlag);
  G_flag_checkpointer_P_write_log = (__typeof__(G_flag_checkpointer_P_write_log)) (beginFlags + 4*sizeOfExitFlag);
  G_flag_checkpointer_P_last_safe_ts = (__typeof__(G_flag_checkpointer_P_last_safe_ts)) (beginFlags + 6*sizeOfExitFlag);
  G_flag_checkpointer_G_next = (__typeof__(G_flag_checkpointer_G_next)) (beginFlags + 8*sizeOfExitFlag); // sizeof(int32_t) != sizeof(void*)


  *G_flag_checkpointer_exit = 0;
  *G_flag_checkpointer_ready = 0;
  *G_flag_checkpointer_done = 0;

  nvmalloc_thr_priv_base_ptr = malloc(nbThreads * sizeof(void*));
  nvmalloc_thr_shar_base_ptr = malloc(nbThreads * sizeof(void*));
  nvmalloc_thr_shar_size += logBytesPerThread;
  nvmalloc_size += sharedBytes;

  char localMallocFile0[1024];
  char localMallocFile1[1024];
  sprintf(localMallocFile0, "%s%i", NVMALLOC_THREAD_PRIV_FILE, 0);
  sprintf(localMallocFile1, "%s%i", NVMALLOC_THREAD_PRIV_FILE, 1);

  // long poolAreas = nbThreads <= 1 ? 1 : 32; // TODO
  
  // GUY::assuming they wanted half as many max threads since they use 32 in the 
  // existing count above and in their commented out code below
  long poolAreas = nbThreads <= 1 ? 1 : (MAX_THREAD / 2); 
  
  // long poolAreasNUMA0 = nbThreads <= 32 ? nbThreads : 32;
  // long poolAreasNUMA1 = nbThreads > 32 ? 1 : nbThreads - 32;

  long mapFlag = MAP_PRIVATE;
  void *addrNUMA0 = alocateInNVRAM(nvram_regions[0], localMallocFile0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2, mapFlag, NULL);
  void *addrNUMA1 = alocateInNVRAM(nvram_regions[1], localMallocFile1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2, mapFlag, NULL);

  // TODO: test if using HTM on MAP_SHARED is slower
  nvmalloc0_base_ptr = ((char*)addrNUMA0) + nvmalloc_thr_priv_size * poolAreas;
  nvmalloc1_base_ptr = ((char*)addrNUMA1) + nvmalloc_thr_priv_size * poolAreas;
  // for (long i = 0; i < nvmalloc_size / sizeof(intptr_t); i += 4096 / sizeof(intptr_t)) {
  //   readSharedMem = ((intptr_t*)nvmalloc_base_ptr)[i];
  //   __atomic_store_n(&((intptr_t*)nvmalloc_base_ptr)[i], readSharedMem, __ATOMIC_RELEASE);
  //   if (i > 1048576) break;
  // }
  nvmalloc0_current_ptr = nvmalloc0_base_ptr;
  nvmalloc1_current_ptr = nvmalloc1_base_ptr;

  char logMallocFile0[1024];
  char logMallocFile1[1024];
  sprintf(logMallocFile0, "%s%i", NVMALLOC_THREAD_SHAR_FILE, 0);
  sprintf(logMallocFile1, "%s%i", NVMALLOC_THREAD_SHAR_FILE, 1);

#ifdef USE_MAP_SYNC
  #ifndef DISABLE_PMEM
    void *sharNUMA0 = alocateInNVRAM(nvram_regions[0], logMallocFile0,
      // nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);
      nvmalloc_thr_shar_size * poolAreas, MAP_SHARED_VALIDATE|MAP_SYNC, NULL);

    void *sharNUMA1 = alocateInNVRAM(nvram_regions[1], logMallocFile1,
      // nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);
      nvmalloc_thr_shar_size * poolAreas, MAP_SHARED_VALIDATE|MAP_SYNC, NULL);
  #else
    void *sharNUMA0 = alocateInNVRAM(nvram_regions[0], logMallocFile0,
      nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);

    void *sharNUMA1 = alocateInNVRAM(nvram_regions[1], logMallocFile1,    
      nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);
  #endif
#else 
  //original code
  void *sharNUMA0 = alocateInNVRAM(nvram_regions[0], logMallocFile0,
      nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);

  void *sharNUMA1 = alocateInNVRAM(nvram_regions[1], logMallocFile1,    
    nvmalloc_thr_shar_size * poolAreas, /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, NULL);
#endif

  printf("\n\n---------------------------------\n");
  printf("---------------------------------\n");
  printf("WARNING:\n");
  printf("SPHT Numa node 0 is being computed as TID < MAX_HYPER_THREADS / 2 and:\n");
  printf("SPHT Numa node 1 is being computed as TID >= MAX_HYPER_THREADS / 2 and:\n");
  printf("Change this if you want a different numa node computation\n");
  printf("---------------------------------\n");
  printf("---------------------------------\n\n");

  int countNUMAThr0 = 0;
  int countNUMAThr1 = 0;
  for (int i = 0; i < nbThreads ; ++i) {      
      //GUY:: it is not clear why they compute the node ID like this, this suggests
      // that they want to bind worker threads round-robin between NUMA node 0 and node 1
      // which is terrible for performance doing this would also not be computed by i%2
      // it would be: nodeId = i / (MAX_THREAD / NUM_NUMA_NODES); 
      // int nodeId = i % 2; // TODO: this one is round-robin
      int nodeId = 0;

      if (i >= MAX_HYPER_THREADS / 2) {
        nodeId = 1;
      }

      if (nodeId == 0) {
        nvmalloc_thr_priv_base_ptr[i] = ((char*)addrNUMA0) + nvmalloc_thr_priv_size * countNUMAThr0;
        nvmalloc_thr_shar_base_ptr[i] = ((char*)sharNUMA0) + nvmalloc_thr_shar_size * countNUMAThr0;
        // for (long j = 0; j < nvmalloc_thr_priv_size / sizeof(intptr_t) - 1; j += 4096 / sizeof(intptr_t)) {
        //   readSharedMem = ((intptr_t*)nvmalloc_thr_priv_base_ptr[countNUMAThr0])[j];
        //   __atomic_store_n(&((intptr_t*)nvmalloc_thr_priv_base_ptr[countNUMAThr0])[j], readSharedMem, __ATOMIC_RELEASE);
        //   // if (j > 1048576) break;
        // }
        countNUMAThr0++;
      } else {
        nvmalloc_thr_priv_base_ptr[i] = ((char*)addrNUMA1) + nvmalloc_thr_priv_size * countNUMAThr1;
        nvmalloc_thr_shar_base_ptr[i] = ((char*)sharNUMA1) + nvmalloc_thr_shar_size * countNUMAThr1;
        // for (long j = 0; j < nvmalloc_thr_priv_size / sizeof(intptr_t) - 1; j += 4096 / sizeof(intptr_t)) {
        //   readSharedMem = ((intptr_t*)nvmalloc_thr_priv_base_ptr[countNUMAThr1])[j];
        //   __atomic_store_n(&((intptr_t*)nvmalloc_thr_priv_base_ptr[countNUMAThr1])[j], readSharedMem, __ATOMIC_RELEASE);
        //   // if (j > 1048576) break;
        // }
        countNUMAThr1++;
      }

  }

#ifndef DISABLE_PHTM
  #ifndef FORK_CHECKPOINTER
    fflush(stdout);   
    checkPointerArgs.nbThreads = nbThreads;
    checkPointerArgs.addrNUMA0 = addrNUMA0;
    checkPointerArgs.addrNUMA1 = addrNUMA1;
    printf("Starting checkpointer thread\n");
    pthread_create(&checkPointerThread, NULL, checkPointerThreadFunc, (void*)((uintptr_t)&checkPointerArgs));
  #else
    signal(SIGCHLD,handleSIGCHLD);
    
    if (fork() == 0) { // child process
      printf("Forked child checkpointer process\n");
      // VERY IMPORTANT!!!
      // munmap the previous addresses and mmap them again in MAP_SHARED | MAP_FIXED mode

      pidChildProc = getpid();
      printf("Child pid=%d\n", pidChildProc);

      void *prevAddr0 = addrNUMA0;
      void *prevAddr1 = addrNUMA1;

      if (munmap(addrNUMA0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2)) {
        fprintf(stderr, "Error munmap %s: %s\n", NVMALLOC_FILE, strerror(errno));
      }
      if (munmap(addrNUMA1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2)) {
        fprintf(stderr, "Error munmap %s: %s\n", NVMALLOC_FILE, strerror(errno));
      }

#ifdef USE_MAP_SYNC
  #ifndef DISABLE_PMEM
        addrNUMA0 = alocateInNVRAM(nvram_regions[0], localMallocFile0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
          // /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr0);
          MAP_SHARED_VALIDATE|MAP_SYNC, prevAddr0);
        addrNUMA1 = alocateInNVRAM(nvram_regions[1], localMallocFile1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
          // /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr1);
          MAP_SHARED_VALIDATE|MAP_SYNC, prevAddr1);
  #else
        addrNUMA0 = alocateInNVRAM(nvram_regions[0], localMallocFile0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
          /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr0);      
        addrNUMA1 = alocateInNVRAM(nvram_regions[1], localMallocFile1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
          /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr1);        
  #endif
#else
      //original code
      addrNUMA0 = alocateInNVRAM(nvram_regions[0], localMallocFile0, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
          /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr0);      
      addrNUMA1 = alocateInNVRAM(nvram_regions[1], localMallocFile1, nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2,
        /*MAP_SHARED_VALIDATE|MAP_SYNC*/MAP_SHARED, prevAddr1);        
#endif
      nvmalloc0_base_ptr = ((char*)addrNUMA0) + nvmalloc_thr_priv_size * poolAreas;
      nvmalloc1_base_ptr = ((char*)addrNUMA1) + nvmalloc_thr_priv_size * poolAreas;
      nvmalloc0_current_ptr = nvmalloc0_base_ptr;
      nvmalloc1_current_ptr = nvmalloc1_base_ptr;

      nvramRanges[2*nbNvramRanges] = addrNUMA0;
      nvramRanges[2*nbNvramRanges+1] = (void*)(((uintptr_t)addrNUMA0) + nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2);
      nbNvramRanges++;

      nvramRanges[2*nbNvramRanges] = addrNUMA1;
      nvramRanges[2*nbNvramRanges+1] = (void*)(((uintptr_t)addrNUMA1) + nvmalloc_thr_priv_size * poolAreas + nvmalloc_size / 2);
      nbNvramRanges++;
      
      checkpointer();
    }
  #endif
#else 
  __atomic_store_n(G_flag_checkpointer_ready, 1, __ATOMIC_RELEASE);
#endif  

  // int g1 = __atomic_load_n(G_flag_checkpointer_ready, __ATOMIC_ACQUIRE);
  // printf("G_flag_checkpointer_ready=%d\n", g1);

  // TODO: if the 
  while(!__atomic_load_n(G_flag_checkpointer_ready, __ATOMIC_ACQUIRE)) {
    pthread_yield(); // wait the checkpointer
    usleep(100);
  }

  // int g2 = __atomic_load_n(G_flag_checkpointer_ready, __ATOMIC_ACQUIRE);
  // printf("G_flag_checkpointer_ready=%d\n", g2);

  return (void*)(readSharedMem ^ readSharedMem); /* make sure the child reads the mem! */
}

static void *nvmalloc_local_impl(int threadId, size_t bytes, int useThreadLocalVars, int isShared)
{
  intptr_t curPtr, basePtr;
  volatile void *addr;
  // static __thread uint64_t accumulatedBytes = 0;

  if (isShared) {
    // shared
    if (useThreadLocalVars) {
      if (nvmalloc_thr_shar_current_ptr == NULL) {
        nvmalloc_thr_shar_current_ptr = nvmalloc_thr_shar_base_ptr[threadId];
        nvmalloc_thr_shar_base_ptr2 = nvmalloc_thr_shar_base_ptr[threadId];
      }
      addr = nvmalloc_thr_shar_current_ptr;
      nvmalloc_thr_shar_current_ptr = (void*)((intptr_t)nvmalloc_thr_shar_current_ptr + bytes);
      curPtr = (intptr_t)nvmalloc_thr_shar_current_ptr;
      basePtr = (intptr_t)nvmalloc_thr_shar_base_ptr2;
    } else {
      addr = nvmalloc_thr_shar_base_ptr[threadId];
      nvmalloc_thr_shar_base_ptr[threadId] = (void*)((intptr_t)nvmalloc_thr_shar_base_ptr[threadId] + bytes);
      curPtr = (intptr_t)nvmalloc_thr_shar_base_ptr[threadId];
      basePtr = (intptr_t)nvmalloc_thr_shar_base_ptr[threadId];
    }

    if (curPtr > basePtr + nvmalloc_thr_shar_size) {
      addr = (void*)-1;
      fprintf(stderr, "[nvmalloc_local]: shared alloc out of space\n");
    }
  } else {
    // private
    if (useThreadLocalVars) {
      if (nvmalloc_thr_priv_current_ptr == NULL) {
        nvmalloc_thr_priv_current_ptr = nvmalloc_thr_priv_base_ptr[threadId];
        nvmalloc_thr_priv_base_ptr2 = nvmalloc_thr_priv_base_ptr[threadId];
      }
      addr = nvmalloc_thr_priv_current_ptr;
      nvmalloc_thr_priv_current_ptr = (void*)((intptr_t)nvmalloc_thr_priv_current_ptr + bytes);
      curPtr = (intptr_t)nvmalloc_thr_priv_current_ptr;
      basePtr = (intptr_t)nvmalloc_thr_priv_base_ptr2;
    } else {
      addr = nvmalloc_thr_priv_base_ptr[threadId];
      nvmalloc_thr_priv_base_ptr[threadId] = (void*)((intptr_t)nvmalloc_thr_priv_base_ptr[threadId] + bytes);
      curPtr = (intptr_t)nvmalloc_thr_priv_base_ptr[threadId];
      basePtr = (intptr_t)nvmalloc_thr_priv_base_ptr[threadId];
    }

    if (curPtr > basePtr + nvmalloc_thr_priv_size) {
      addr = (void*)-1;
      fprintf(stderr, "[nvmalloc_local]: private alloc out of space (MAX = %lu)\n", nvmalloc_thr_priv_size);
    }
  }
  // accumulatedBytes += bytes;
  // printf("[%i] nvmalloc_local = %p (%zu B, acc = %zu)\n", threadId, addr, bytes, accumulatedBytes);
  return (void*)addr;
}

void *nvmalloc_local(int threadId, size_t bytes)
{
  return nvmalloc_local_impl(threadId, bytes, 0/* useThreadLocalVars */, 0/* !isShared */);
}

void *nvmalloc(size_t bytes)
{
  intptr_t addr;
  if (__sync_fetch_and_add(&nvmalloc_count, 1) % 2) {
    addr = __sync_fetch_and_add((intptr_t*)&nvmalloc0_current_ptr, bytes);
    if (addr + bytes > (intptr_t)nvmalloc0_base_ptr + (nvmalloc_size / 2)) {
      addr = -1;
      fprintf(stderr, "[nvmalloc]: out of space (total space = %zu, alloc = %zu)\n",
        nvmalloc_size / 2, bytes);
    }
  } else {
    addr = __sync_fetch_and_add((intptr_t*)&nvmalloc1_current_ptr, bytes);
    if (addr + bytes > (intptr_t)nvmalloc1_base_ptr + (nvmalloc_size / 2)) {
      addr = -1;
      fprintf(stderr, "[nvmalloc]: out of space (total space = %zu, alloc = %zu)\n",
        nvmalloc_size / 2, bytes);
    }
  }
  return (void*)addr;
}

void nvfree(void *ptr)
{
  /* TODO */
}

void global_structs_init(
  int nbThreads,
  int nbReplayers,
  uint64_t allocEpochs,
  uint64_t allocLogSize,
  uint64_t localMallocSize,
  uint64_t sharedMallocSize,
  char *nvram_regions[]
) {

#ifdef GUY_SPHT_NUM_REPLAYERS
  nbReplayers = GUY_SPHT_NUM_REPLAYERS;
#endif
  printf("NB_REPLAYERS = %i\n", nbReplayers);

  // TODO: check whether the log pointers are persistent or not

  nvmalloc_thr_priv_size = localMallocSize;

  if (HTM_read_only_storage1_size > sizeof(cache_line_s)) {
    gs_appInfo = (cache_line_s*)HTM_read_only_storage1;
  } else {
    EASY_MALLOC(gs_appInfo, 1);
  }

  printf("nbThreads=%d\n", nbThreads);
  printf("nbReplayers=%d\n", nbReplayers);

  gs_appInfo->info.isExit          = 0;
  gs_appInfo->info.nbThreads       = nbThreads;
  gs_appInfo->info.nbReplayers     = nbReplayers;
  gs_appInfo->info.allocEpochs     = allocEpochs;
  gs_appInfo->info.allocLogSize    = allocLogSize;
  gs_appInfo->info.localMallocSize = localMallocSize;
  gs_appInfo->info.epochTimeout    = EPOCH_TIMOUT;
  gs_appInfo->info.GUY_UNUSED_spinsFlush      = 0;

  gs_log_data.log.epoch_end        = allocEpochs - 1;
  gs_log_data.log.who_is_pruning   = -1;
 
  nvmalloc_init(nbThreads, allocLogSize*sizeof(uint64_t) + allocEpochs*sizeof(uint64_t), sharedMallocSize,
    nvram_regions);

  EASY_MALLOC(gs_ts_array, nbThreads);
  memset((void*)gs_ts_array, 0, sizeof(large_cache_line_s)*nbThreads);

  EASY_MALLOC(G_observed_ts, nbThreads);
  memset((void*)G_observed_ts, 0, sizeof(large_cache_line_s)*nbThreads);


  G_next = nvmalloc_local_impl(0, sizeof(cache_line_s) * nbThreads, 0, 1);
  memset((void*)G_next, 0, sizeof(cache_line_s)*nbThreads);
  *G_flag_checkpointer_G_next = G_next;

  P_epoch_ts = nvmalloc(sizeof(uint64_t*) * nbThreads);
  P_epoch_persistent = nvmalloc(sizeof(uint64_t) * allocEpochs);

  P_write_log =  nvmalloc_local_impl(0, sizeof(uint64_t*) * nbThreads, 0, 1);
  *G_flag_checkpointer_P_write_log = P_write_log;

  P_last_safe_ts = nvmalloc_local_impl(0, sizeof(large_cache_line_s), 0, 1);
  *G_flag_checkpointer_P_last_safe_ts = &(P_last_safe_ts->ts);

  memset((void*)P_last_safe_ts, 0, sizeof(large_cache_line_s));

  EASY_MALLOC(G_epoch_lock, allocEpochs);
  memset((void*)G_epoch_lock, 0, sizeof(int)*allocEpochs);
  for (int i = 0; i < nbThreads; ++i) {   
    P_epoch_ts[i] = nvmalloc_local_impl(i, sizeof(uint64_t) * allocEpochs, 0, 1);
    memset((void*)P_epoch_ts[i], 0, sizeof(uint64_t)*allocEpochs);

    uint64_t *startLogMarker = nvmalloc_local_impl(i, sizeof(uint64_t) * 3, 0, 1);
    startLogMarker[0] = (uint64_t)-1;
    startLogMarker[1] = (uint64_t)-1;
    startLogMarker[2] = (uint64_t)-1;
    P_write_log[i] = nvmalloc_local_impl(i, sizeof(uint64_t) * allocLogSize, 0, 1);

    // P_write_log does not need initialization
    G_observed_ts[i].ts = (uint64_t)-1;
    G_next[i].log_ptrs.flush_epoch = allocEpochs;
  }
}

void nvmalloc_print_stats(char *filename)
{
  FILE *fp = fopen(filename, "a+");
  if (fp == NULL) {
    printf("Cannot open %s! Try to remove it.\n", filename);
    return;
  }
  fseek(fp, 0L, SEEK_END);
  if ( ftell(fp) < 8 ) {
      fprintf(fp, "#%s\t%s\t%s\n",
              "NB_MALLOCS",
              "NB_BYTES_NUMA0",
              "NB_BYTES_NUMA1");
  }
  fprintf(fp, "%lu\t%lu\t%lu\n", nvmalloc_count,
    (uintptr_t)nvmalloc0_current_ptr - (uintptr_t)nvmalloc0_base_ptr,
    (uintptr_t)nvmalloc1_current_ptr - (uintptr_t)nvmalloc1_base_ptr);
}

void global_structs_destroy()
{
#ifdef PROFILE_FALLBACK_PATH_TIME
  //GUY TODO: 2.1 = CPU_FREQ_GHZ
  double fallbackTime = (double)(timeAbortedTX_global) / 2.1 / 1000000000;
  printf("fallback_path_time=%lf\n", fallbackTime);
#endif

  free((void*)gs_ts_array);
  nvfree((void*)G_next); // TODO
  for (int i = 0; i < gs_appInfo->info.nbThreads; ++i) {
    nvfree((void*)P_epoch_ts[i]);
    nvfree((void*)P_write_log[i]);
  }
  nvfree((void*)P_epoch_ts);
  nvfree((void*)P_write_log);
  if (gs_appInfo != (cache_line_s*)HTM_read_only_storage1) {
    free((void*)gs_appInfo);
  }
  printf("checkpointer starting threads: G_flag_checkpointer_exit set to 1\n");
  __atomic_store_n(G_flag_checkpointer_exit, 1, __ATOMIC_RELEASE);

#ifndef DISABLE_PHTM
  while(!__atomic_load_n(G_flag_checkpointer_done, __ATOMIC_ACQUIRE)) {
    pthread_yield(); // wait the checkpointer
    usleep(100);
  }
  printf("joining checkpointer thread\n");
  #ifndef FORK_CHECKPOINTER
    pthread_join(checkPointerThread, NULL);
  #else
    wait(NULL);
  #endif
#endif
  // TODO: erase the file
}
