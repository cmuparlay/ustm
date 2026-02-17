/*
 * Multiverse is a software TM that features versioned and unversioned transactions.
 *      It uses dynamic multiversing to associate version lists with
 *      individual (64-bit) words so long as those versions are likely to be used by versioned transactions
 *
 * Details:
 *  Lock table entries have lock + bloom filter indicating if the addr is versioned
 *      there is a corresponding table of version lists where
 *      an entry is an initially small number of contiguous version lists
 *      expandable (but obviously not contiguously)
 *
 *
 * The TM framework is based on the TrinityVRTL2 implementation from
 *   the authors of Trinity: Pedro Ramalhete: pramalhe@gmail.com, Andreia Correia: andreia.veiga@unine.ch
 *   and Pascal Felber: pascal.felber@unine.ch
 *
 *   The original trinity interface is from https://github.com/pramalhe/durabletx/blob/master/ptms/trinity/TrinityVRTL2.hpp
 */
#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <cstring>
#include <thread>       // Needed by this_thread::yield()
#include <sys/mman.h>   // Needed if we use mmap()
#include <sys/types.h>  // Needed by open() and close()
#include <sys/stat.h>
#include <linux/mman.h> // Needed by MAP_SHARED_VALIDATE
#include <fcntl.h>
#include <unistd.h>     // Needed by close()
#include <filesystem>   // Needed by std::filesystem::space()
#include <type_traits>
#include <sched.h>      // sched_setaffinity()
#include <csetjmp>      // Needed by sigjmp_buf
#include <cstdlib>      // Needed by exit()
#include <chrono>
#include <vector>
#include <bit>

#ifndef USTM_X86
#undef USE_RDTSC //dont use x86-specific hardware timestamping
#endif

#ifdef USTM_PPC
#warning in our experiments, multiverse reaches an infinite loop on power-pc
#endif

#include "tm_common/stats.h"
#include "tm_common/tm_debug.h"
#include "tm_common/tm_timelines.h"
#include "bloom_filter.h"

std::atomic<int> debugLock {0};

// #define TRACK_OP_TYPE 1
#ifdef TRACK_OP_TYPE
thread_local int OP_TYPE;
#endif

// #define USE_TM_ALLOC 1

#if defined(USE_TM_ALLOC)
    #include "tm_common/tm_alloc2.h"
#endif

namespace ns_multiverse {

#ifdef USE_RDTSC
uint64_t rdtsc(){
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (uint64_t) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
}
uint64_t rdtsc_get_read_ts_relaxed() {
    // std::atomic_thread_fence(std::memory_order_seq_cst);
    return rdtsc();
}
uint64_t rdtsc_get_read_ts() {
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return rdtsc();
}
uint64_t rdtsc_get_abort_ts() {
    uint64_t ts = rdtsc();
    while(ts == rdtsc()) {}
    std::atomic_thread_fence(std::memory_order_seq_cst);
    return ts+1;
}
#endif
  
#define FORCE_NO_INLINE __attribute__((noinline))
#define FORCE_INLINE __attribute__((always_inline))
#define FORCE_INLINE2 __attribute__((always_inline))

#define PRINT_MAX_ABORTS_DEBUG_INFO 1

/*
 *******************************************
 * These macros enable / disable debug modes
 *******************************************
*/ 

// #define DISABLE_UNVERSIONING 1
// #define DISABLE_TM_FREE 1
// #define DISABLE_MODE2_SNAPSHOTS

// #define TESTING_FORCE_MODE2_READS 1
// #define TESTING_FORCE_SNAPSHOT_READS 1

// #define FORCE_MODE2_FROM_START 1
// #define PRINT_DEBUG_VTN_STATS 1

// #define DEBUG_SPECIAL 1

/*
 *******************************************
 * These macros enable / disable various stat tracking
 *******************************************
*/ 

// #define TRACK_TOTAL_READ_TIME
// #define TRACK_VTN_DEPTH 1
// #define TRACK_VLIST_TRAVERAL_DEPTH 1

#define TRACK_SNAPSHOT_METRICS      1
#define TRACK_SNAPSHOT_METRICS_2    1
#define TRACK_REGULAR_METRICS       1



// This macro enables colocating bloom filters with the versioned locks
// #define COLOCATE_BLOOM_FILTER 1



/*
 *******************************************
 * Tunable parameters
 *******************************************
 *******************************************
*/ 



// max aborts for update txns + an unused fail-safe for mode 1 versioned transactions
static const uint64_t MAX_ABORTS = 1000000;

static const int MIN_RD_SET_SIZE_BEFORE_UPDATING_THRESHOLD = 256;

static const uint64_t RETRIES_BEFORE_CHECKING_READ_SET_SIZE_FOR_MODE2 = 16;
static const uint64_t RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE = 100; //snapshot readers version
static const uint64_t RETRIES_BEFORE_SNAPSHOT_MODE2 = 128; //tm writers version


static std::atomic<uint64_t> MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE {0}; // If a snapshot aborts with read set size greater than this constant then we force it to mode 2 immediately

static const uint64_t MAX_SNAPSHOT_MODE2_RETRIES_BEFORE_LOCKING = 1024*1024ULL;

static const int MIN_SMALL_TXNS_TO_UNSTICK_MODE2 = 10;

#if defined(USE_TM_ALLOC) && !defined(VOLATILE_MEMORY_REGION_SIZE)
    #define VOLATILE_MEMORY_REGION_SIZE 32*1024*1024*1024ULL //GB
#endif

// Maximum number of registered threads that can execute transactions
static const int REGISTRY_MAX_THREADS = MAX_THREADS_POW2;

static const uint64_t NUM_LOCKS = 64*1024*1024;

/*
 *******************************************
 *******************************************
*/ 


// TL2 constants
static const uint64_t LOCKED = 0x8000000000000000ULL;

typedef int txn_type;
static const txn_type TX_IS_NONE                     = 0;
static const txn_type TX_IS_READ                     = 2;
static const txn_type TX_IS_UPDATE                   = 4;

static const txn_type TX_IS_SNAPSHOT                 = 1; //tx_type & TX_IS_SNAPSHOT == true (regular snapshots provide opacity)
static const txn_type TX_IS_SNAPSHOT_MODE2           = 3;

#ifdef ENABLE_SNAPSHOT_ISOLATION
static const txn_type TX_IS_SNAPSHOT_ISOLATION       = 5;
#endif

inline int get_bit_width(uint64_t num) {
    int bitWidth = 0;
    while (num) {
        bitWidth++;
        num >>= 1;
    }
    return bitWidth;
}

static const uint64_t murmurSeed = 0xFFFFFFFFFFFFFFC5ULL;

uint64_t MurmurHash64A (const void * key, int len, uint64_t seed) {
    const uint64_t m = 0xc6a4a7935bd1e995LLU;
    const int r = 47;

    uint64_t h = seed ^ (len * m);

    const uint64_t * data = (const uint64_t *)key;
    const uint64_t * end = (len >> 3) + data;

    while(data != end) {
        uint64_t k = *data++;

        k *= m;
        k ^= k >> r;
        k *= m;

        h ^= k;
        h *= m;
    }

    const unsigned char * data2 = (const unsigned char *)data;

    switch(len & 7) {
        case 7: h ^= (uint64_t)(data2[6]) << 48;
        case 6: h ^= (uint64_t)(data2[5]) << 40;
        case 5: h ^= (uint64_t)(data2[4]) << 32;
        case 4: h ^= (uint64_t)(data2[3]) << 24;
        case 3: h ^= (uint64_t)(data2[2]) << 16;
        case 2: h ^= (uint64_t)(data2[1]) << 8;
        case 1: h ^= (uint64_t)(data2[0]);
                h *= m;
    };

    h ^= h >> r;
    h *= m;
    h ^= h >> r;

    return h;
}

inline __attribute__((always_inline)) static uint64_t hidx(size_t addr) {
    // return (((addr + 128) >> 2) & (NUM_LOCKS-1)); //mapping function from TL2
    return ((addr >> 6) & (NUM_LOCKS-1));  // Thanks to Pedro Ramalhete for suggesting this improved mapping function
}

static const int TID_SHIFT = 53;
static const int LOCK_SHIFT = 63;

static const uint64_t LOCK_MASK = 0x8000000000000000ULL;
static const uint64_t TID_MASK = 0x7fe0000000000000ULL;
static const uint64_t TS_MASK = 0x001fffffffffffffULL;

static const uint64_t VERSIONING_TID = (1<<(LOCK_SHIFT-TID_SHIFT))-1;

// Helper functions
inline FORCE_INLINE2 static bool isLocked(uint64_t sl) {
    return (sl & LOCKED);
}

inline FORCE_INLINE2 static bool isUnlocked(uint64_t sl) {
    return (!(sl & LOCKED));
}

inline FORCE_INLINE2 static uint64_t getTid(uint64_t sl) {
    return ((sl & TID_MASK) >> TID_SHIFT);
}

inline FORCE_INLINE2 static uint64_t getTS(uint64_t sl) {
    return (sl & TS_MASK);
}

inline FORCE_INLINE2 static bool isReadConsistent(uint64_t rClock, uint64_t tid, uint64_t sl) {
    return ((isUnlocked(sl) && getTS(sl) < rClock) || (getTid(sl) == tid));
}

inline FORCE_INLINE2 static bool isInprogressVersioning(uint64_t sl) {
    return (getTid(sl) == VERSIONING_TID);
}

inline FORCE_INLINE2 static bool isWriteConsistent(uint64_t rClock, uint64_t tid, uint64_t sl) {
    return isReadConsistent(rClock, tid, sl);
}

inline FORCE_INLINE2 static bool tryWriteLock(uint64_t tid, std::atomic<uint64_t>* mutex, uint64_t sl) {
    if (isLocked(sl)) {
        if (getTid(sl) == tid) {
            return true;
        }
        else {
            return false;
        }
    }
    else {
        assert(isUnlocked(sl));
        uint64_t newSL = (LOCKED) | (tid << TID_SHIFT) | (getTS(sl));
        assert(getTid(newSL) == tid);
        return mutex->compare_exchange_strong(sl, newSL);
    }
}
inline FORCE_INLINE2 static bool versioning_tryWriteLock(uint64_t tid, std::atomic<uint64_t>* mutex, uint64_t sl) {
    if (isLocked(sl)) {
        return false;
    }
    else {
        assert(isUnlocked(sl));
        uint64_t newSL = (LOCKED) | (tid << TID_SHIFT) | (getTS(sl));
        assert(getTid(newSL) == tid);
        return mutex->compare_exchange_strong(sl, newSL);
    }
}
static const uint64_t EMPTY_SNAPSHOT_TIMESTAMP = (uint64_t)(0x4000000000000000);
static const uint64_t TBD_SNAPSHOT_MASK = 0x8000000000000000ULL;

inline FORCE_INLINE2 static uint64_t makeTBDSnapshotTimestamp(uint64_t ts) {
    return (ts | TBD_SNAPSHOT_MASK);
}

inline FORCE_INLINE2 static bool isTBD(uint64_t ts) {
    return (ts & TBD_SNAPSHOT_MASK);
}

inline FORCE_INLINE2 static uint64_t getUnmarkedTimestamp(uint64_t markedTs) {
    return (markedTs & (~TBD_SNAPSHOT_MASK));
}

struct padded_atomic_long {
    std::atomic<long> data;
    PAD;
};

struct padded_atomic_u64 {
    std::atomic<uint64_t> data;
    PAD;
};

struct padded_u64 {
    uint64_t data;
    PAD;
};

struct VersionedNode {
    std::atomic<uint64_t> ts;
    uint64_t data;          //constant after allocation/init
    VersionedNode* next;    //constant after allocation/init
};

struct VListTableNode {
    uint64_t* addr;         //constant after allocation/init
    VListTableNode* next;   //constant after allocation/init
    std::atomic<VersionedNode*> listHead;
    uint64_t unused;
};

struct LockTableNode {
    std::atomic<uint64_t> mutex;
#ifdef COLOCATE_BLOOM_FILTER
    ns_multiverse::BloomFilter64Volatile bloom;
#endif
};

struct Globals {
    PAD;
    std::atomic<uint64_t> gClock {1};
    PAD;
    // std::atomic<long> ___gEpoch {0};
    // PAD;
    std::atomic<long> gTmModeCounter {0};
    padded_atomic_long* gThreadModeAndTxType; //this is heap allocated by the bg thread to ensure that it is local to the bg threads numa node
    padded_atomic_long* gThreadMode2Sticky; //this is heap allocated by the bg thread to ensure that it is local to the bg threads numa node
    PAD;
    std::atomic<long> gFirstObservedMode2Timestamp {0};
    PAD;
    LockTableNode gHashLock[NUM_LOCKS];
    PAD;
#ifndef COLOCATE_BLOOM_FILTER
    ns_multiverse::BloomFilter64Volatile blooms[NUM_LOCKS];
    PAD;
#endif
    std::atomic<VListTableNode*> gVlistTable[NUM_LOCKS];
    PAD;
    // padded_atomic_long ___gThreadEpochs[MAX_THREADS_POW2];
    // PAD;
    padded_atomic_u64 gThreadSnapTsUpdatesBeforeCommit[MAX_THREADS_POW2];
    PAD;
};

extern Globals globals;


template <int BUFF_SIZE>
struct VoidPtrBuffer {
    void* list[BUFF_SIZE];
    uint64_t size;
    VoidPtrBuffer* next;

    void reset() {
        if (next) {
            next->reset();
        }
        size = 0;
    }
};

static const int EBR_MAX_LIMBO_BAG_SIZE = 62;
static const uint64_t MAX_ALLOC_BUFFER_LIST_SIZE = 62;

static const uint64_t EBR_RETRY_THRESHOLD = 10 * MAX_THREADS_POW2;
static const double milliseconds_between_epoch_updates = 20.0;

struct VoidPtrBufferNode {
    void* ptr;
    VoidPtrBufferNode* next;

    VoidPtrBufferNode() {}
    VoidPtrBufferNode(void* _ptr, VoidPtrBufferNode* _next) {
        ptr = _ptr;
        next = _next;
    }
};

using LimboBagNode = VoidPtrBufferNode;
using AllocBufferNode = VoidPtrBufferNode;

struct EBR_State {
    LimboBagNode* activeTxnLimboBag;
    LimboBagNode* activeTxnLimboBagTail;
    LimboBagNode* prevEpochLimboBag;
    LimboBagNode* currEpochLimboBag;
    long currEpoch;
    uint64_t ClearBagRetries;
    std::chrono::time_point<std::chrono::system_clock> sysTime;

    void reset() {
        while (activeTxnLimboBag) {
            LimboBagNode* tmp = activeTxnLimboBag;
            activeTxnLimboBag = activeTxnLimboBag->next;
            delete tmp;
        }
    }
};

struct AllocInfo {
    uint64_t tid;
    EBR_State myEbrState;
    AllocBufferNode* mallocList = nullptr;
    uint64_t retireClears = 0;
    PAD;
    #ifdef TRACK_EBR_ALLOCS
    uint64_t numAllocs = 0;
    #endif

    void reset() {
        myEbrState.reset();
        AllocBufferNode* tmp;
        while (mallocList) {
            tmp = mallocList;
            mallocList = mallocList->next;
            delete tmp;
        }
        mallocList = nullptr;
    }
};

alignas(128) extern thread_local AllocInfo* tl_allocInfo;

class MemoryManager;
extern MemoryManager gMemManager;

class MemoryManager {
    #ifdef USE_TM_ALLOC
    TMAlloc tmAlloc;
    #endif

public:
    AllocInfo perThreadAllocInfo[REGISTRY_MAX_THREADS];
    PAD;
    std::atomic<long> gEpoch {0};
    PAD;
    padded_atomic_long gThreadEpochs[MAX_THREADS_POW2];
    PAD;

    MemoryManager() {
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            perThreadAllocInfo[i].tid = i;
            perThreadAllocInfo[i].myEbrState.sysTime = std::chrono::system_clock::now();
            perThreadAllocInfo[i].myEbrState.currEpoch = 0;
            perThreadAllocInfo[i].myEbrState.ClearBagRetries = 0;
            gThreadEpochs[i].data.store(-1, std::memory_order_relaxed);
        }
    }

    #ifdef USE_TM_ALLOC
    inline void init(void* baseAddr, size_t mapSize) {
        tmAlloc.init(baseAddr, mapSize);
    }
    #endif

    inline void freePrevEpochLimboBag(AllocInfo* myAllocInfo) {
        EBR_State* ebr = &myAllocInfo->myEbrState;
        freeBuffContents(ebr->prevEpochLimboBag);
        ebr->prevEpochLimboBag = ebr->currEpochLimboBag;
        ebr->currEpochLimboBag = nullptr;
        ebr->currEpoch = gEpoch.load();
    }

    inline void commit(AllocInfo* allocInfo) {
        #ifdef USE_TM_ALLOC
        tmAlloc.commit(allocInfo->tid);
        #endif

        EBR_State* ebr = &allocInfo->myEbrState;

        if (ebr->activeTxnLimboBag) {
            ebr->activeTxnLimboBagTail->next = ebr->currEpochLimboBag;
            ebr->currEpochLimboBag = ebr->activeTxnLimboBag;
            ebr->activeTxnLimboBag = nullptr;
            ebr->activeTxnLimboBagTail = nullptr;
        }

        if (ebr->currEpoch + 1 < gEpoch.load()) {
            freePrevEpochLimboBag(allocInfo);
            allocInfo->retireClears++;
        }
        std::chrono::time_point<std::chrono::system_clock> timeNow = std::chrono::system_clock::now();
        ebr->ClearBagRetries++;
        if (ebr->ClearBagRetries == EBR_RETRY_THRESHOLD  ||
            std::chrono::duration_cast<std::chrono::milliseconds>(timeNow - ebr->sysTime).count() >
            milliseconds_between_epoch_updates * (1 + ((float) allocInfo->tid)/REGISTRY_MAX_THREADS)) {
            ebr->ClearBagRetries = 0;
            ebr->sysTime = timeNow;
            tryAdvanceGlobalEpoch();
        }
    }

    inline void abort(uint64_t tid) {
        #ifdef USE_TM_ALLOC
        tmAlloc.abort(tid);
        #else
        freeBuffContents(tl_allocInfo->mallocList);
        tl_allocInfo->mallocList = nullptr;
        #endif
        // tl_allocInfo->myEbrState.onAbort();
    }

    inline void announceEpoch(uint64_t tid) {
        long currentEpoch = gEpoch.load();
        gThreadEpochs[tid].data.store(currentEpoch, std::memory_order_relaxed);
    }

    inline void unanounceEpoch(uint64_t tid) {
        gThreadEpochs[tid].data.store(-1l, std::memory_order_relaxed);
    }

    inline void forceClearBags() {
        while (tl_allocInfo->myEbrState.currEpochLimboBag || tl_allocInfo->myEbrState.prevEpochLimboBag) {
            announceEpoch(tl_allocInfo->tid);
            commit(tl_allocInfo);
        }
    }

    inline void tryAdvanceGlobalEpoch() {
        long currentEpoch = gEpoch.load();
        bool allThreadsAtCurrentEpoch = true;
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            long otherEpoch = gThreadEpochs[i].data.load();
            if (otherEpoch != -1 && otherEpoch < currentEpoch) {
                allThreadsAtCurrentEpoch = false;
                break;
            }
        }
        if (allThreadsAtCurrentEpoch) {
            gEpoch.compare_exchange_strong(currentEpoch, currentEpoch+1);
        }
    }

    inline void freeBuffContents(VoidPtrBufferNode* buff) {
        while (buff) {
            VoidPtrBufferNode* tmp = buff;
            buff = buff->next;
            deallocate(tmp->ptr);
            delete tmp;
        }
    }

    inline VoidPtrBufferNode* addItemToBuffer(VoidPtrBufferNode* buff, void* ptr) {
        VoidPtrBufferNode* newNode = new VoidPtrBufferNode();
        #ifdef TRACK_EBR_ALLOCS
        tl_allocInfo->numAllocs++;
        #endif
        newNode->ptr = ptr;
        newNode->next = buff;
        return newNode;
    }

    inline void retireIrrevocable(void* ptr) {
        tl_allocInfo->myEbrState.currEpochLimboBag = addItemToBuffer(tl_allocInfo->myEbrState.currEpochLimboBag, ptr);
    }

    inline void retireWithPossibleRollback(void* ptr) {
        tl_allocInfo->myEbrState.activeTxnLimboBag = addItemToBuffer(tl_allocInfo->myEbrState.activeTxnLimboBag, ptr);
        if (!tl_allocInfo->myEbrState.activeTxnLimboBag->next) {
            tl_allocInfo->myEbrState.activeTxnLimboBagTail = tl_allocInfo->myEbrState.activeTxnLimboBag;
        }
    }

    inline void* allocate(size_t size) {
        void* ptr = nullptr;
        #if defined(USE_TM_ALLOC)
        ptr = tmAlloc.doMalloc(tl_allocInfo->tid, size);
        #else
        ptr = malloc(size);
        #endif
        assert(ptr != nullptr);
        return ptr;
    }

    inline void* bufferedAllocate(size_t size) {
        void* ptr = nullptr;
        #if defined(USE_TM_ALLOC)
        ptr = tmAlloc.tmMalloc(tl_allocInfo->tid, size);
        // ptr = tmAlloc.doMalloc(tl_allocInfo->tid, size);
        // tl_allocInfo->mallocList = addItemToBuffer(tl_allocInfo->mallocList, ptr);
        #else
        ptr = malloc(size);
        tl_allocInfo->mallocList = addItemToBuffer(tl_allocInfo->mallocList, ptr);
        #endif
        assert(ptr != nullptr);
        return ptr;
    }

    inline void deferFree(void* ptr) {
        assert(ptr != nullptr);
        retireWithPossibleRollback(ptr);
    }

    inline void deallocate(void* ptr) {
        assert(ptr != nullptr);
        #if defined(USE_TM_ALLOC)
        uint64_t tid = tl_allocInfo->tid;
        tmAlloc.doFree(tid, ptr);
        #else
        free(ptr);
        #endif
    }
};



static void unannounceThreadTmMode(int tid) {
    globals.gThreadModeAndTxType[tid].data.store(-1l, std::memory_order_relaxed);
    globals.gThreadMode2Sticky[tid].data.store(0, std::memory_order_relaxed);
}




// Random number generator used by the backoff scheme
inline FORCE_INLINE static uint64_t marsagliaXORV(uint64_t x) {
    if (x == 0) x = 1;
    x ^= x << 6;
    x ^= x >> 21;
    x ^= x << 7;
    return x;
}


//
// Thread Registry stuff
//
extern void thread_registry_deregister_thread(const int tid);

// An helper class to do the checkin and checkout of the thread registry
struct ThreadCheckInCheckOut {
    static const int NOT_ASSIGNED = -1;
    int tid { NOT_ASSIGNED };
    ~ThreadCheckInCheckOut() {
        if (tid == NOT_ASSIGNED) return;
        thread_registry_deregister_thread(tid);
    }
};

extern thread_local ThreadCheckInCheckOut tl_tcico;

class ThreadRegistry;
extern ThreadRegistry gThreadRegistry;

/*
 * <h1> Registry for threads </h1>
 *
 * This is singleton type class that allows assignement of a unique id to each thread.
 * The first time a thread calls ThreadRegistry::getTID() it will allocate a free slot in 'usedTID[]'.
 * This tid wil be saved in a thread-local variable of the type ThreadCheckInCheckOut which
 * upon destruction of the thread will call the destructor of ThreadCheckInCheckOut and free the
 * corresponding slot to be used by a later thread.
 */
class ThreadRegistry {
private:
    alignas(128) std::atomic<bool>      usedTID[REGISTRY_MAX_THREADS];   // Which TIDs are in use by threads
    alignas(128) std::atomic<int>       maxTid {-1};                     // Highest TID (+1) in use by threads

public:
    ThreadRegistry() {
        for (int it = 0; it < REGISTRY_MAX_THREADS; it++) {
            usedTID[it].store(false, std::memory_order_relaxed);
        }
    }

    // Progress condition: wait-free bounded (by the number of threads)
    int register_thread_new(void) {
        for (int tid = 0; tid < REGISTRY_MAX_THREADS; tid++) {
            if (usedTID[tid].load(std::memory_order_acquire)) continue;
            bool unused = false;
            if (!usedTID[tid].compare_exchange_strong(unused, true)) continue;
            // Increase the current maximum to cover our thread id
            int curMax = maxTid.load();
            while (curMax <= tid) {
                maxTid.compare_exchange_strong(curMax, tid+1);
                curMax = maxTid.load();
            }
            tl_tcico.tid = tid;
            return tid;
        }
        printf("ERROR: Too many threads, registry can only hold %d threads\n", REGISTRY_MAX_THREADS);
        assert(false);
        return -1;
    }

    // Progress condition: wait-free population oblivious
    inline void deregister_thread(const int tid) {
        unannounceThreadTmMode(tid);
        gMemManager.forceClearBags();
        gMemManager.unanounceEpoch(tid);
        usedTID[tid].store(false, std::memory_order_release);
    }

    inline void reserveSpecificTid(const int tid) {
        bool unused = false;
        if (!usedTID[tid].compare_exchange_strong(unused, true)) {
            printf("Error could not reserve specific thread id\n");
            exit(-1);
        }
    }

    // Progress condition: wait-free population oblivious
    static inline uint64_t getMaxThreads(void) {
        return gThreadRegistry.maxTid.load(std::memory_order_acquire);
    }

    // Progress condition: wait-free bounded (by the number of threads)
    static inline int getTID(void) {
        int tid = tl_tcico.tid;
        if (tid != ThreadCheckInCheckOut::NOT_ASSIGNED) return tid;
        return gThreadRegistry.register_thread_new();
    }
};



// // Needed by our benchmarks and to prevent destructor from being called automatically on abort/retry
// struct tmbase {
//     ~tmbase() {
//         printf("tm base destroyed\n");
//     }
// };


struct ReadSet {
    struct ReadSetEntry {
        std::atomic<uint64_t>* mutex;
    };

    static const int64_t MAX_ENTRIES = 16*1024;

    uint64_t      size {0};
    ReadSetEntry  entries[MAX_ENTRIES];    
    ReadSet*      next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    inline bool validate(uint64_t rClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            std::atomic<uint64_t>* mutex = entries[i].mutex;
            uint64_t sl = mutex->load(std::memory_order_acquire);
            while (isInprogressVersioning(sl)) {
                sl = mutex->load(std::memory_order_acquire);
            }
            if (!isReadConsistent(rClock, tid, sl)) {
                return false;
            }
        }
        if (next != nullptr) return next->validate(rClock, tid);
        return true;
    }

    inline void add(std::atomic<uint64_t>* mutex) {
        if (size != MAX_ENTRIES) {
            entries[size].mutex = mutex;
            size++;
        } else {
            if (next == nullptr) {
                next = new ReadSet();
            }
            next->add(mutex);
        }
    }

    bool contains(std::atomic<uint64_t>* mutex) {
        for (uint64_t i = 0; i < size; i++) {
            std::atomic<uint64_t>* m = entries[i].mutex;
            if (m == mutex) {
                return true;
            }
        }
        if (next != nullptr) return next->contains(mutex);
        return true;
    }
};

struct AppendLog {
    static const int64_t MAX_ENTRIES = 7*1024;

    struct LogEntry {
        uint64_t* addr;
        uint64_t  oldValue;
    };

    int64_t       size {0};
    LogEntry      entries[MAX_ENTRIES];
    AppendLog*    next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    inline void add(uint64_t* addr, uint64_t oldValue) {
        if (size != MAX_ENTRIES) {
            entries[size].addr = addr;
            entries[size].oldValue = oldValue;
            size++;
        } else {
            if (next == nullptr) {
                next = new AppendLog();
            }
            next->add(addr, oldValue);
        }
    }

    void rollback(uint64_t tid) {
        if (size == 0) return;
        if (next != nullptr) {
            next->rollback(tid);
        }
        for (int64_t i = size-1; i >= 0; i--) {
            *(entries[i].addr) = entries[i].oldValue;
        }
    }

    inline void unlock(uint64_t nextClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            uint64_t* addr = entries[i].addr;
            std::atomic<uint64_t>* mutex = &globals.gHashLock[hidx((size_t)addr)].mutex;
            uint64_t sl = mutex->load(std::memory_order_relaxed);
            if (isLocked(sl) && getTid(sl) == tid) {
                uint64_t newSL = (tid << TID_SHIFT) | (nextClock);
                mutex->store(newSL, std::memory_order_release);
            }
        }
        if (next != nullptr) {
            next->unlock(nextClock, tid);
        }
    }
};


struct AppendLog2 {
    // static const int64_t MAX_ENTRIES = 512;
    static const int64_t MAX_ENTRIES = 756;

    struct LogEntry {
        VListTableNode* vtn;
    };


    int64_t             size {0};
    LogEntry            entries[MAX_ENTRIES];
    AppendLog2*         next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    inline void add(VListTableNode* _vtn) {
        if (size != MAX_ENTRIES) {
            entries[size].vtn = _vtn;
            size++;
        } else {
            if (next == nullptr) {
                next = new AppendLog2();
            }
            next->add(_vtn);
        }
    }

    inline void rollback(uint64_t tid) {
        if (size == 0) return;
        if (next != nullptr) {
            next->rollback(tid);
        }
        for (int64_t i = size-1; i >= 0; i--) {
            VListTableNode* vtn = entries[i].vtn;
            VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
            vNode->ts.store(EMPTY_SNAPSHOT_TIMESTAMP, std::memory_order_relaxed);
            vtn->listHead.store(vNode->next, std::memory_order_relaxed);
            gMemManager.retireIrrevocable(vNode);
        }
    }

    inline void removeTBDs(uint64_t tid, uint64_t lastReadSnapshotTimestamp) {
        if (next != nullptr) {
            next->removeTBDs(tid, lastReadSnapshotTimestamp);
        }
        for (uint64_t i = 0; i < size; i++) {
            // This addr cannot become unversioned while we hold the lock on it
            VListTableNode* vtn = entries[i].vtn;
            VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
            vNode->ts.store(lastReadSnapshotTimestamp, std::memory_order_release);
        }
    }
};

typedef int tm_mode_type;

static tm_mode_type TM_MODE_DEFAULT          = 0;
static tm_mode_type TM_MODE_INTERMEDIATE1    = 1;
static tm_mode_type TM_MODE_MODE2            = 2;
static tm_mode_type TM_MODE_INTERMEDIATE2    = 3;

// Thread-local data
// An array of OpData is heap allocated in the constructor of the TM object
// This data is likely too large to stack allocate
// Thanks to Pedro Ramalhete for suggesting some optimizations to the memory layout of OpData
struct OpData {
    alignas(64) uint64_t        rClock {0};
    uint64_t        tid;    
    txn_type        tx_type {TX_IS_NONE};        
    ReadSet         readSet;
    AppendLog       writeSet;
    uint64_t        myrand;
    std::jmp_buf    env;
    uint64_t        attempt {0};

    long            myTmModeCounter {0};
    tm_mode_type    myTmMode {TM_MODE_DEFAULT};    
    int             readCount {0};    

    uint64_t        snapshotStartTs {0};

    bool            forceVersioning {false};
    bool            forceMode2Reads {false};
    int             smallTxnReadCount {0};
    int             consecutiveSmallTxns {0};

    
    AppendLog2      snapshotWriteSet;

    bool            thisTxnForcedMode2Swap {false};
    int             readCountAtFirstMode2Commit {0};
    int             numSnapReadsPastHead {0};


    //debug and stat tracking related info
    #if defined(TRACK_TIME_BEFORE_COMMIT) || defined(TRACK_TIME_BEFORE_ABORT)
    std::chrono::time_point<std::chrono::system_clock> txnStartTime;
    #endif

    #ifdef TRACK_SNAPSHOT_METRICS_2
    uint64_t        numTryVersionsDuringTxn {0};
    uint64_t        numFailedTryVersionsDuringTxn {0};
    #endif

    #ifdef TRACK_VLIST_TRAVERAL_DEPTH
    uint64_t        vlistCount[50];
    #endif

    #ifdef TRACK_VTN_DEPTH
    uint64_t        vtnDepth[50];
    #endif

    #ifdef TRACK_TOTAL_READ_TIME
    double          totalReadTime {0};
    double          maxReadTime {0};
    uint64_t        retriesAtMaxReadTime {0};
    uint64_t        totalReads {0};
    #endif

    #ifdef DEBUG_SPECIAL
    uint64_t modeCounterAtStartTxn {0};
    uint64_t modeCounterMode2Transition {0};
    uint64_t oldest_ts_at_last_abort {(uint64_t)(-1)};
    #endif

    // uint64_t snapshotAttempts {0};

    PAD;
};

inline FORCE_INLINE static void findAddrInVLT(OpData* myd, void* addr, uint64_t tableIndex, VListTableNode* &retVtn, bool &retBool) {
    assert(tableIndex >= 0 && tableIndex < NUM_LOCKS);
    VListTableNode* vtn = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
    bool found = false;

    #ifdef TRACK_VTN_DEPTH
    uint64_t depth = 1;
    #endif

    while (vtn) {
        if (vtn->addr == addr) {
            found = true;
            break;
        }
        vtn = vtn->next;
        #ifdef TRACK_VTN_DEPTH
        depth++;
        #endif
    }
    #ifdef TRACK_VTN_DEPTH
    // uint64_t log2Index = std::bit_width(depth) - 1;
    uint64_t log2Index = get_bit_width(depth) - 1;
    myd->vtnDepth[log2Index] += 1;
    #endif
    retVtn = vtn;
    retBool = found;
}


[[noreturn]] extern void abortTx(OpData* myd);

alignas(128) extern thread_local OpData* tl_opdata;


static const int TX_TYPE_BIT_SHIFT = 3;
static const long TX_TYPE_MASK = 7l;

static const int TM_MODE_BIT_SHIFT = 2;
static const int TM_MODE_MASK = 3l;


static inline int getCounterMode(long tmModeCounter) {
    return (tmModeCounter & TM_MODE_MASK);
}

static inline long getUnmarkedTmModeCounter(long tmModeCounter) {
    return tmModeCounter >> TM_MODE_BIT_SHIFT;
}

static inline long combineTmModeAndTxType(long tmModeCounter, int txType) {
    return (tmModeCounter << TX_TYPE_BIT_SHIFT) | (txType);
}

static inline int getThreadTxType(long threadTmModeAndTxType) {
    return threadTmModeAndTxType & TX_TYPE_MASK;
}

static inline long getThreadTmModeCounter(long threadTmModeAndTxType) {
    return threadTmModeAndTxType >> TX_TYPE_BIT_SHIFT;
}

static inline void waitForDefaultModeWriters(long tmModeCounter) {
    long unmarkedTmModeCounter = getUnmarkedTmModeCounter(tmModeCounter);
    while (true) {
        bool found = false;
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            long threadTmModeAndTxType = globals.gThreadModeAndTxType[i].data;
            if (threadTmModeAndTxType != -1) {
                long counter = getUnmarkedTmModeCounter(getThreadTmModeCounter(threadTmModeAndTxType));
                int txType = getThreadTxType(threadTmModeAndTxType);
                #ifdef ENABLE_SNAPSHOT_ISOLATION
                if (txType == TX_IS_UPDATE || txType == TX_IS_SNAPSHOT_ISOLATION) {
                #else
                if (txType == TX_IS_UPDATE) {
                #endif
                    if (counter < unmarkedTmModeCounter) {
                        found = true;
                        break;
                    }
                }
                //We do not need to wait for non-update txns
                //since they can only become updaters if they
                //reread the global version which will force them to mode 2
            }
        }
        if (!found) {
            return;
        }
    }
}

static inline uint64_t waitForStickyMode2(uint64_t* myrand) {
    bool found = false;
    uint64_t numFound = 0;
    while (true) {
        *myrand = marsagliaXORV(*myrand);
        uint64_t stall = (*myrand & numFound) + 1;
        stall *= 16;
        std::atomic<uint64_t> iter {0};
        while (iter.load() < stall) iter.fetch_add(1);
        if (stall > 1000) std::this_thread::yield();

        found = false;
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            if (globals.gThreadMode2Sticky[i].data) {
                numFound++;
                found = true;
                break;
            }
        }
        if (!found) {
            return numFound;
        }
    }
}

static inline uint64_t waitForMode2Snapshotters(uint64_t* myrand, long tmModeCounter) {
    uint64_t numFound = 0;
    long unmarkedTmModeCounter = getUnmarkedTmModeCounter(tmModeCounter);
    while (true) {
        *myrand = marsagliaXORV(*myrand);
        uint64_t stall = (*myrand & numFound) + 1;
        stall *= 16;
        std::atomic<uint64_t> iter {0};
        while (iter.load() < stall) iter.fetch_add(1);
        if (stall > 1000) std::this_thread::yield();

        bool found = false;
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            long threadTmModeAndTxType = globals.gThreadModeAndTxType[i].data.load();
            if (threadTmModeAndTxType != -1) {
                long counter = getUnmarkedTmModeCounter(getThreadTmModeCounter(threadTmModeAndTxType));
                int txType = getThreadTxType(threadTmModeAndTxType);
                //we have to wait for any txn with a counter less than the global
                //since it could become a mode 2 snapshot on its next txn
                if (counter < unmarkedTmModeCounter) {
                    found = true;
                    numFound++;
                    break;
                }
                if (counter == unmarkedTmModeCounter && (txType == TX_IS_SNAPSHOT_MODE2)) {
                    found = true;
                    numFound++;
                    break;
                }
            }
        }
        if (!found) {
            return numFound;
        }
    }
}

static void announceThreadTmMode(OpData* myd) {
    long gTmModeCounter = globals.gTmModeCounter.load();
    int currentTmMode = getCounterMode(gTmModeCounter);
    myd->myTmMode = currentTmMode;
    myd->myTmModeCounter = gTmModeCounter;

    if (currentTmMode != TM_MODE_DEFAULT) {
        if (currentTmMode == TM_MODE_INTERMEDIATE2 && myd->tx_type == TX_IS_SNAPSHOT_MODE2) {
            myd->tx_type = TX_IS_SNAPSHOT;
            STATS_COUNTER_ADD(myd->tid, num_mode2_forced_back_to_mode1, 1);
            TM_TIMELINE_END((int)myd->tid, "mode2_snapshot_cutoff");
            // printf("tid %ld had mode2 went to mode1\n", myd->tid);
            // TM_TIMELINE_END_INMEM((int)myd->tid, mode2_snapshot_cutoff);
        }
        if (currentTmMode == TM_MODE_MODE2 && myd->tx_type == TX_IS_SNAPSHOT) {
            myd->tx_type = TX_IS_SNAPSHOT_MODE2;
            TM_TIMELINE_START((int)myd->tid);
        }
        //if we are in mode 2 or transitioning writers must version
        myd->forceVersioning = true;
    }

    long tmModeAndTxType = combineTmModeAndTxType(gTmModeCounter, myd->tx_type);
    // globals.gThreadModeAndTxType[myd->tid].data.exchange(tmModeAndTxType);
    globals.gThreadModeAndTxType[myd->tid].data.store(tmModeAndTxType, std::memory_order_relaxed);

    //TODO::IMPORTANT:: do we need to do this every begin? Probably better to just do it when myd->forceMode2Reads changes
    globals.gThreadMode2Sticky[myd->tid].data.store(myd->forceMode2Reads, std::memory_order_relaxed);
}

static long transitionTmMode(const int tid, long lastSeenTmModeCounter, int desiredTmMode) {
    if (getCounterMode(lastSeenTmModeCounter) == desiredTmMode) {
        return -1;
    }
    long newTmTmModeCounter = (((lastSeenTmModeCounter >> TM_MODE_BIT_SHIFT) + 1) << TM_MODE_BIT_SHIFT) | (desiredTmMode);
    if (globals.gTmModeCounter.compare_exchange_strong(lastSeenTmModeCounter, newTmTmModeCounter)) {
        DEBUG_PRINT_TM_MODES("transitioned from %ld to %ld\n", lastSeenTmModeCounter, newTmTmModeCounter);
        return newTmTmModeCounter;
    }
    else {
        return -1;
    }
}



static void unversionVtn(const uint64_t tid, uint64_t tableIndex) {
    LockTableNode* ltn = &globals.gHashLock[tableIndex];
    std::atomic<uint64_t>* mutex = &ltn->mutex;
    uint64_t sl = mutex->load(std::memory_order_acquire);
    if (!tryWriteLock(tid, mutex, sl)) {
        return;
    }

    VListTableNode* vtn = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
    VListTableNode* vtn_tmp;
    globals.gVlistTable[tableIndex].store(nullptr, std::memory_order_relaxed);
    #ifdef COLOCATE_BLOOM_FILTER
    ltn->bloom.init();
    #else
    globals.blooms[tableIndex].init();
    #endif

    while (vtn) {
        VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
        gMemManager.retireIrrevocable(vNode);
        vtn_tmp = vtn;
        vtn = vtn->next;
        gMemManager.retireIrrevocable(vtn_tmp);
    }

    uint64_t newSL = (tid << TID_SHIFT) | (sl);
    mutex->store(newSL, std::memory_order_release);
}

alignas(128) uint64_t numUnversions = 0;
alignas(128) volatile bool bgThreadShutdown = false;
alignas(128) volatile bool bgThreadDoneInit = false;
alignas(128) uint64_t last_avgTsDelta = 0;
static const int MAX_DIST_VEC_SIZE = 50;
static const int MIN_DIST_VEC_SORT_SIZE = 10;
static const float AVG_TS_DELTA_DIST_TAIL_CUTOFF = 0.9; //90%
static const int bgThreadTid = REGISTRY_MAX_THREADS-1;

static void bgThreadUnversioner() {
    const int tid = ThreadRegistry::getTID();
    uint64_t myrand = (tid+1)*12345678901234567ULL;
    printf("BG Thread started with tid: %d\n", tid);
    tl_allocInfo = &gMemManager.perThreadAllocInfo[tid];
    std::vector<uint64_t> avgTsDeltaDist;
    avgTsDeltaDist.reserve(MAX_DIST_VEC_SIZE);
    uint64_t numPasses = 0;
    uint64_t avgTotal = 0;
    uint64_t mode2Snapshots_ingress = 0;
    uint64_t mode2Snapshots_engress = 0;

    uint64_t minMode2Found = -1ULL;
    uint64_t maxMode2Found = 0;

    long tmModeCounter;
    int tmMode;
    globals.gThreadModeAndTxType = (padded_atomic_long*)malloc(sizeof(padded_atomic_long) * MAX_THREADS_POW2);
    for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
        globals.gThreadModeAndTxType[i].data.store(-1, std::memory_order_relaxed);
    }

    globals.gThreadMode2Sticky = (padded_atomic_long*)malloc(sizeof(padded_atomic_long) * MAX_THREADS_POW2);
    for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
        globals.gThreadMode2Sticky[i].data.store(0, std::memory_order_relaxed);
    }

    bgThreadDoneInit = true;
    __sync_synchronize();

    std::chrono::time_point<std::chrono::system_clock> time1;
    std::chrono::time_point<std::chrono::system_clock> time2;
    int64_t durationInIntermediateMode1 = 0;
    int64_t durationInMode2 = 0;
    int64_t durationInIntermediateMode2 = 0;
    uint64_t numMode2;

    TM_TIMELINE_START(tid);
    TM_TIMELINE_BLIP(tid, "mode_switch_to_mode1");

    #ifdef FORCE_MODE2_FROM_START
    tmModeCounter = globals.gTmModeCounter.load();
    tmMode = getCounterMode(tmModeCounter);
    if (tmMode != TM_MODE_MODE2) {
        transitionTmMode(tid, tmModeCounter, TM_MODE_MODE2);
    }
    #else 
    while (!bgThreadShutdown) {
        tmModeCounter = globals.gTmModeCounter.load();
        tmMode = getCounterMode(tmModeCounter);
        gMemManager.announceEpoch(tid);

        if (tmMode != TM_MODE_DEFAULT) {
            TM_TIMELINE_BLIP(tid, "mode_switch_to_intermediate1");
            DEBUG_PRINT_TM_MODES("TM mode = %d\n", getCounterMode(tmModeCounter));
            time1 = std::chrono::system_clock::now();
            //wait for default mode writers to drain
            DEBUG_PRINT_TM_MODES("Intermediate1: waiting for default updaters\n");
            waitForDefaultModeWriters(tmModeCounter);

            //transition to mode 2
            tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_MODE2);
            uint64_t currGTimestamp = globals.gClock.load();
            globals.gFirstObservedMode2Timestamp.store(currGTimestamp, std::memory_order_relaxed);
            time2 = std::chrono::system_clock::now();
            durationInIntermediateMode1 += std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
            TM_TIMELINE_BLIP(tid, "mode_switch_to_mode2");
            DEBUG_PRINT_TM("mode_switch_to_mode2\n");

            DEBUG_PRINT_TM_MODES("TM mode = %d\n", getCounterMode(tmModeCounter));
            time1 = std::chrono::system_clock::now();

            //wait until we observe no ongoing mode 2 snapshots
            waitForStickyMode2(&myrand);

            // DEBUG_PRINT_TM_MODES("Mode2: waiting for MODE2 snaphotters\n");
            // numMode2 = waitForMode2Snapshotters(&myrand, tmModeCounter);
            // if (numMode2 > maxMode2Found) { maxMode2Found = numMode2; }
            // if (numMode2 < minMode2Found) { minMode2Found = numMode2; }
            // //check a second time for mode 2 snapshotters
            // numMode2 = waitForMode2Snapshotters(&myrand, tmModeCounter);

            //transition to intermediate 2
            tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_INTERMEDIATE2);
            time2 = std::chrono::system_clock::now();
            durationInMode2 += std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
            TM_TIMELINE_BLIP(tid, "mode_switch_to_intermediate2");
            DEBUG_PRINT_TM("mode_switch_to_intermediate2\n");

            time1 = std::chrono::system_clock::now();
            //wait for phase 2 snapshots to drain
            DEBUG_PRINT_TM_MODES("Intermediate2: waiting for MODE2 snapshotters\n");
            numMode2 = waitForMode2Snapshotters(&myrand, tmModeCounter);
            if (numMode2 > maxMode2Found) { maxMode2Found = numMode2; }
            if (numMode2 < minMode2Found) { minMode2Found = numMode2; }
            numMode2 = waitForMode2Snapshotters(&myrand, tmModeCounter);

            globals.gFirstObservedMode2Timestamp.store((uint64_t)(-1), std::memory_order_release);

            //transition back to default mode
            tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_DEFAULT);
            time2 = std::chrono::system_clock::now();
            durationInIntermediateMode2 += std::chrono::duration_cast<std::chrono::milliseconds>(time2 - time1).count();
            TM_TIMELINE_BLIP(tid, "mode_switch_to_mode1");
            DEBUG_PRINT_TM("mode_switch_to_mode1\n");
        }

        uint64_t total = 0;
        uint64_t count = 0;
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            uint64_t tsDelta = globals.gThreadSnapTsUpdatesBeforeCommit[i].data.load(std::memory_order_relaxed);
            if (tsDelta != 0) {
                total += tsDelta;
                count++;
            }
        }
        if (count <= 0) {
            continue;
        }

        uint64_t avgTsDelta = total / count;

        size_t distSize = avgTsDeltaDist.size();
        if (distSize > MAX_DIST_VEC_SIZE) {
            avgTsDeltaDist.erase(avgTsDeltaDist.begin());
            distSize--;
        }
        avgTsDeltaDist.push_back(last_avgTsDelta);
        distSize++;
        if (distSize > MIN_DIST_VEC_SORT_SIZE) {
            std::sort(avgTsDeltaDist.begin(), avgTsDeltaDist.end());

            size_t distHeadCount = (size_t)((float)distSize * AVG_TS_DELTA_DIST_TAIL_CUTOFF);
            total = 0;
            for (int i = 1; i < distHeadCount+1; i++) {
                total += avgTsDeltaDist[distSize-i];
            }
            avgTsDelta = total / distHeadCount;
        }
        else {
            continue;
        }

        last_avgTsDelta = avgTsDelta;
        avgTotal += avgTsDelta;

        for (int i = 0; i < NUM_LOCKS; i++) {
            if (bgThreadShutdown) {
                break;
            }

            tmModeCounter = globals.gTmModeCounter.load();
            tmMode = getCounterMode(tmModeCounter);
            if (TM_MODE_DEFAULT != tmMode) {
                DEBUG_PRINT_TM_MODES("bg thread loop found non-default mode\n");
                break;
            }

            VListTableNode* vtn = globals.gVlistTable[i].load(std::memory_order_relaxed);
            if (!vtn) {
                continue;
            }
            uint64_t newestTs = 0;
            while (vtn) {
                VersionedNode* head = vtn->listHead.load(std::memory_order_relaxed);
                if (head) {
                    uint64_t ts = head->ts;
                    if (ts > newestTs) {
                        newestTs = ts;
                    }
                }
                vtn = vtn->next;
            }
            #ifndef USE_RDTSC
            uint64_t gTs = globals.gClock.load();
            #else
            uint64_t gTs = rdtsc_get_read_ts();
            #endif
            if (newestTs + avgTsDelta < gTs) {
                #ifndef DISABLE_UNVERSIONING
                unversionVtn(tid, i);
                #endif
                gMemManager.commit(tl_allocInfo);
                numUnversions++;
            }
        }
        numPasses++;
    }
    #endif

    TM_TIMELINE_END(tid, "BG_THREAD_MODE_SWITCHES");

    printf("\nbgThread done\n");
    printf("num passes = %ld\n", numPasses);
    if (numPasses > 0) {
        printf("avg_of_avg_Ts_Deltas=%ld\n", avgTotal / numPasses);
    }
    else {
        printf("avg_of_avg_Ts_Deltas=unavailable due to 0 passes\n");
    }
    printf("minMode2 = %ld\n", minMode2Found);
    printf("maxMode2 = %ld\n", maxMode2Found);

    printf("Milliseconds_in_intermidate1=%ld\n", durationInIntermediateMode1);
    printf("Milliseconds_in_mode2=%ld\n", durationInMode2);
    printf("Milliseconds_in_intermidate2=%ld\n", durationInIntermediateMode2);
    printf("\n");
}


//returns true iff this thread successfully transistions to mode2
//  the system can still be in mode2 if this returns false
//  but the transition will have been done by a different thread
static inline bool transitionToMode2(OpData* myd) {
    globals.gThreadMode2Sticky[myd->tid].data.store(1, std::memory_order_relaxed);
    myd->forceMode2Reads = true;
    myd->thisTxnForcedMode2Swap = true;
    myd->tx_type = TX_IS_SNAPSHOT_MODE2;
    STATS_COUNTER_ADD(tid, mode2_snapshot_starts, 1);
    if (myd->myTmMode == TM_MODE_DEFAULT) {
        if (-1 != transitionTmMode(myd->tid, myd->myTmModeCounter, TM_MODE_INTERMEDIATE1)) {
            DEBUG_PRINT_TM("transitioned_to_intermediate1\n");
            return true;
        }
    }
    STATS_LIST_APPEND(tid, aborts_before_mode2, myd->attempt-1);
    return false;
}


class Multiverse;
extern Multiverse gMultiverse;

class Multiverse {
// private:
public:
    alignas(128) OpData* opDesc;
    alignas(128) std::atomic<int> debugLock {0};
    alignas(128) void* mapAddr;
    alignas(128) std::thread* bgThread;

public:
    // struct tmbase : public ns_multiverse::tmbase { };

    Multiverse() {
        printf("sizeof(OpData)=%ld\n", sizeof(OpData));
        opDesc = new OpData[REGISTRY_MAX_THREADS];
        globals.gFirstObservedMode2Timestamp.store((uint64_t)(-1));
        for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
            opDesc[it].tid = it;
            opDesc[it].myrand = (it+1)*12345678901234567ULL;
            // globals.gThreadEpochs[it].data.store(-1, std::memory_order_relaxed);
            globals.gThreadSnapTsUpdatesBeforeCommit[it].data.store(0, std::memory_order_relaxed);

            for (int x = 0; x < 50; x++) {
                #ifdef TRACK_VLIST_TRAVERAL_DEPTH
                opDesc[it].vlistCount[x] = 0;
                #endif
                #ifdef TRACK_VTN_DEPTH
                opDesc[it].vtnDepth[x] = 0;
                #endif
            }
        }

        #ifdef FORCE_MODE2_FROM_START
        long tmModeCounter = globals.gTmModeCounter.load();
        int tmMode = getCounterMode(tmModeCounter);
        tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_INTERMEDIATE1);
        transitionTmMode(tid, tmModeCounter, TM_MODE_MODE2);
        #endif


        bgThread = new std::thread(bgThreadUnversioner);
        while (!bgThreadDoneInit) {
            //spin
        }

        #ifdef USE_TM_ALLOC
        mapTmMemoryRegion();
        #endif
    }

    ~Multiverse() {
        bgThreadShutdown = true;
        __sync_synchronize();
        bgThread->join();

        // printf("\ngEpoch=%ld\n", globals.gEpoch.load());
        printf("gClock=%ld\n\n", globals.gClock.load());

        uint64_t totalEBRBagClears = 0;
        uint64_t totalEBRRetries = 0;
        for (int it=0; it < gThreadRegistry.getMaxThreads(); it++) {
            totalEBRBagClears += gMemManager.perThreadAllocInfo[it].retireClears;
            totalEBRRetries += gMemManager.perThreadAllocInfo[it].myEbrState.ClearBagRetries;
        }
        printf("tx_num_ebr_limbo_bag_clears=%ld\n", totalEBRBagClears);
        printf("tx_num_ebr_retries=%ld\n", totalEBRRetries);
        // printf("tx_ebr_gepoch=%ld\n\n", globals.gEpoch.load());

        printf("num_unversions=%ld\n", numUnversions);
        printf("last_avgTsDelta=%ld\n\n", last_avgTsDelta);

        #ifdef PRINT_DEBUG_VTN_STATS
        uint64_t total_unusedVtnSlots = 0;
        uint64_t total_usedVtnSlots = 0;
        uint64_t total_versionedAddrs = 0;
        uint64_t max_versionedAddrInSingleSlot = 0;
        uint64_t min_versionedAddrInSingleSlot = (uint64_t)(-1);

        uint64_t log2Histogram_perSlotNumVersionedAddrs[50];
        for (int i = 0; i < 50; i++) {
            log2Histogram_perSlotNumVersionedAddrs[i] = 0;
        }

        for (uint64_t i = 0; i < NUM_LOCKS; i++) {
            uint64_t versionedAddrsInThisSlot = 0;
            VListTableNode* vtn = globals.gVlistTable[i].load(std::memory_order_relaxed);
            while (vtn) {
                versionedAddrsInThisSlot++;
                vtn = vtn->next;
            }
            if (versionedAddrsInThisSlot != 0) {
                total_usedVtnSlots++;

                // uint64_t log2Index = std::bit_width(versionedAddrsInThisSlot) - 1;
                uint64_t log2Index = get_bit_width(versionedAddrsInThisSlot) - 1;
                log2Histogram_perSlotNumVersionedAddrs[log2Index] += 1;
                total_versionedAddrs += versionedAddrsInThisSlot;
                if (versionedAddrsInThisSlot > max_versionedAddrInSingleSlot) {
                    max_versionedAddrInSingleSlot = versionedAddrsInThisSlot;
                }
                if (versionedAddrsInThisSlot < min_versionedAddrInSingleSlot) {
                    min_versionedAddrInSingleSlot = versionedAddrsInThisSlot;
                }
            }
            else {
                total_unusedVtnSlots++;
            }
        }
        printf("\n");
        printf("Num vtn table slots with 0 versioned addrs: %ld\n", total_unusedVtnSlots);
        printf("Num vtn table slots with at least 1 vrsioned addrs: %ld\n", total_usedVtnSlots);
        printf("Total number of vrsioned addrs: %ld\n", total_versionedAddrs);
        printf("max versioned addrs in single slot: %ld\n", max_versionedAddrInSingleSlot);
        printf("min versioned addrs in single slot: %ld\n", min_versionedAddrInSingleSlot);
        printf("\n\n");

        printf("Num versioned addrs per slot:\n");
        for (int x = 0; x < 50; x++) {
            printf("(2^%d-2^%d] %ld\n", x-1, x, log2Histogram_perSlotNumVersionedAddrs[x]);
        }
        printf("\n\n");
        #endif

        printf("global_tm_mode=%d\n", getCounterMode(globals.gTmModeCounter.load()));
        printf("global_tm_counter=%ld\n", getUnmarkedTmModeCounter(globals.gTmModeCounter.load()));
        uint64_t numTransitions = getUnmarkedTmModeCounter(globals.gTmModeCounter.load()) / 4;
        printf("num_mode_changes=%ld\n", numTransitions);
        printf("\n\n");

        #ifdef TRACK_VLIST_TRAVERAL_DEPTH
        uint64_t vlistCounts[50];
        for (int x = 0; x < 50; x++) {
            vlistCounts[x] = 0;
        }

        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            for (int x = 0; x < 50; x++) {
                vlistCounts[x] += opDesc[it].vlistCount[x];
            }
        }
        printf("\n");

        printf("Vlist traversal lengths:\n");
        for (int x = 0; x < 50; x++) {
            printf("(2^%d-2^%d] %ld\n", x-1, x, vlistCounts[x]);
        }
        printf("\n");
        #endif

        #ifdef TRACK_VTN_DEPTH
        uint64_t vtnDepthCounts[50];
        for (int x = 0; x < 50; x++) {
            vtnDepthCounts[x] = 0;
        }

        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            for (int x = 0; x < 50; x++) {
                vtnDepthCounts[x] += opDesc[it].vtnDepth[x];
            }
        }

        printf("Vtn depth lengths:\n");
        for (int x = 0; x < 50; x++) {
            printf("(2^%d-2^%d] %ld\n", x-1, x, vtnDepthCounts[x]);
        }
        printf("\n");
        #endif

        #ifdef TRACK_TOTAL_READ_TIME
        printf("Average duration of a single addr mode2 snapshot read per thread:\n");
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            if (opDesc[it].totalReads != 0) {
                double avgReadTime = opDesc[it].totalReadTime / opDesc[it].totalReads;
                if (avgReadTime != 0) {
                    printf("%f, ", avgReadTime);
                }
            }
        }
        printf("\n");
        printf("Max duration of a single addr mode2 snapshot read and num retries for that read per thread:\n");
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            if (opDesc[it].totalReads != 0) {
                if (opDesc[it].retriesAtMaxReadTime != 0) {
                    printf("%f-%ld, ", opDesc[it].maxReadTime, opDesc[it].retriesAtMaxReadTime);
                }
            }
        }
        printf("\n");
        #endif

        #ifdef TRACK_NUM_USED_LOCKS
        int numUsedLocks = 0;
        for (int i = 0; i < NUM_LOCKS; i++) {
            if (globals.gHashLock[i].mutex.load()) {
                numUsedLocks++;
            }
        }
        printf("num_occupied_locks=%d\n", numUsedLocks);
        printf("percentage_of_occupied_locks=%f\n", ((numUsedLocks * 1.0f) / NUM_LOCKS) * 100.0f);
        #endif

        #ifdef TRACK_VTN_DEPTH_HISTOGRAM
        uint64_t vtn_depthHistogram[256];
        uint64_t vlist_depthHistogram[256];
        for (int i = 0; i < 256; i++) {
            vtn_depthHistogram[i] = 0;
            vlist_depthHistogram[256];
        }
        for (int i = 0; i < NUM_LOCKS; i++) {
            VListTableNode* vtn = globals.gVlistTable[i].load(std::memory_order_relaxed);
            uint64_t depth = 0;
            while (vtn) {
                depth++;
                //we cannot track vlist depth here since some of the vnodes might be retired but still linked in the vlist
                // this is acceptable for us since the ts will prevent threads from accessing those nodes
                // (a thread will see a ts that is less-or-equal to than their own and stop traversing)
                // VersionedNode* vnode = vtn->listHead.load(std::memory_order_relaxed);
                // uint64_t vlist_depth = 0;
                // while (vnode) {
                //     vlist_depth++;
                //     vnode = vnode->next;
                // }
                // int bitWidth = 0;
                // while (vlist_depth) {
                //     bitWidth++;
                //     vlist_depth >>= 1;
                // }
                // vlist_depthHistogram[bitWidth] += 1;

                vtn = vtn->next;
            }
            int bitWidth = 0;
            while (depth) {
                bitWidth++;
                depth >>= 1;
            }
            vtn_depthHistogram[bitWidth] += 1;
        }
        printf("vtn_depth_histogram_data=");
        for (int i = 0; i < 256; i++) {
            printf("%lu,", vtn_depthHistogram[i]);
        }
        printf("\n\n");
        // printf("vlist_depth_histogram_data=");
        // for (int i = 0; i < 256; i++) {
        //     printf("%lu,", vlist_depthHistogram[i]);
        // }
        // printf("\n\n");
        #endif

        #ifdef DEBUG_TM_MODES
        printf("WARNING: DEBUG_TM_MODES is enabled\n");
        printf("\n\n");
        #endif

        delete[] opDesc;

        #ifdef USE_TM_ALLOC
        munmap(mapAddr, VOLATILE_MEMORY_REGION_SIZE);
        #endif

        #ifdef TRACK_EBR_ALLOCS
        uint64_t allocCount = 0;
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            allocCount += gMemManager.perThreadAllocInfo[it].numAllocs;
        }
        printf("mem_manager_num_allocs=%ld\n", allocCount);
        #endif

        #ifdef NO_OPTIMIZE_IS_ON
        printf("\n");
        printf("\n");
        printf("WARNING: THIS BINARY WAS COMPILED WITH NO OPTMIZATIONS. This is not appropriate for experiment results.\n");
        printf("NO_OPTMIZATIONS_IS_ON=1\n");
        printf("\n");
        printf("\n");
        #endif
    }

    #ifdef USE_TM_ALLOC
    void mapTmMemoryRegion() {
        mapAddr = mmap(NULL, VOLATILE_MEMORY_REGION_SIZE, (PROT_READ | PROT_WRITE), MAP_SHARED | MAP_ANONYMOUS, -1, 0);

        if (mapAddr == MAP_FAILED) {
            perror("mmap error:\n ");
            printf("-------------\n");
            exit(-1);
        }

        gMemManager.init(mapAddr, VOLATILE_MEMORY_REGION_SIZE);
    }
    #endif

    inline void beginTx(OpData* myd, const int tid) {
        gMemManager.announceEpoch(myd->tid);
        announceThreadTmMode(myd);

        #ifndef USE_RDTSC
        myd->rClock = globals.gClock.load();
        #else
        myd->rClock = rdtsc_get_read_ts();
        #endif
        if (myd->tx_type & TX_IS_SNAPSHOT) {
            TM_TIMELINE_START((int)myd->tid);
            if (myd->snapshotStartTs == 0) {
                myd->snapshotStartTs = myd->rClock;
            }
        }

        // Clear the logs of the previous transaction
        myd->writeSet.reset();
        myd->snapshotWriteSet.reset();
        myd->readSet.reset();
        tl_allocInfo->reset();
        myd->readCount = 0;
    }

    inline bool endTx(OpData* myd, const int tid) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (myd->writeSet.size == 0 && myd->snapshotWriteSet.size == 0) {
            #ifdef TRACK_REGULAR_METRICS
            STATS_COUNTER_ADD(myd->tid, commits, 1);
            #endif

            if (myd->tx_type & TX_IS_SNAPSHOT) {
                #ifndef USE_RDTSC
                uint64_t gTs = globals.gClock.load(std::memory_order_relaxed);
                #else
                uint64_t gTs = rdtsc_get_read_ts_relaxed();
                #endif
                globals.gThreadSnapTsUpdatesBeforeCommit[tid].data = gTs - myd->snapshotStartTs;
                myd->snapshotStartTs = 0;

                #ifdef TRACK_SNAPSHOT_METRICS
                STATS_COUNTER_ADD(myd->tid, commits_snapshot, 1);
                #endif
                #ifdef TRACK_SNAPSHOT_METRICS_2
                STATS_LIST_APPEND(myd->tid, snapshot_num_successful_tryVersions_per_txn, myd->numTryVersionsDuringTxn);
                STATS_LIST_APPEND(myd->tid, snapshot_num_failed_tryVersions_per_txn, myd->numFailedTryVersionsDuringTxn);
                #endif

                if (myd->tx_type == TX_IS_SNAPSHOT_MODE2) {
                    uint64_t maxRdSetSize = MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.load(std::memory_order_relaxed);
                    if (myd->readCount >= MIN_RD_SET_SIZE_BEFORE_UPDATING_THRESHOLD && (maxRdSetSize == 0 || myd->readCount < maxRdSetSize)) {
                        MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.exchange(myd->readCount, std::memory_order_relaxed);
                        printf("MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE=%d\n", myd->readCount);
                    }

                    #ifdef TRACK_SNAPSHOT_METRICS
                    STATS_COUNTER_ADD(myd->tid, commits_mode2_snapshot, 1);
                    #endif

                    // is mode2 sticky for our thread?
                    if (myd->forceMode2Reads) {
                        if(myd->thisTxnForcedMode2Swap) {
                            myd->readCountAtFirstMode2Commit = myd->readCount;

                            //If we forced the transition to mode2 we make our required smallTxnReadCount to be our read count / threshold number of small txn to leave mode2
                            myd->smallTxnReadCount = myd->readCount / MIN_SMALL_TXNS_TO_UNSTICK_MODE2;
                            myd->consecutiveSmallTxns = 0;
                            myd->thisTxnForcedMode2Swap = false;
                        }
                        else {
                            if (myd->readCount <= myd->smallTxnReadCount) {
                                myd->consecutiveSmallTxns++;
                            }

                            #ifndef TESTING_FORCE_MODE2_READS
                            if (myd->numSnapReadsPastHead == 0 || myd->consecutiveSmallTxns >= MIN_SMALL_TXNS_TO_UNSTICK_MODE2) {
                                myd->readCountAtFirstMode2Commit = 0;
                                myd->consecutiveSmallTxns = 0;
                                myd->forceMode2Reads = false;
                            }
                            #endif
                        }
                    }

                    if (myd->readCount >= 10000) {
                        TM_TIMELINE_END((int)myd->tid, "mode2_snapshot");
                        // TM_TIMELINE_END_INMEM((int)myd->tid, mode2_snapshot);
                    }
                    else {
                        TM_TIMELINE_END((int)myd->tid, "too_small_mode2");
                    }
                }
                else {
                    TM_TIMELINE_END((int)myd->tid, "mode1_snapshot");
                    // TM_TIMELINE_END_INMEM((int)myd->tid, mode1_snapshot);
                }
            }

            #ifdef TRACK_TOTAL_READ_TIME
            myd->totalReads += myd->readCount;
            #endif

            #ifdef TRACK_TIME_BEFORE_COMMIT
            std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
            int64_t dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - myd->txnStartTime).count();
            STATS_LIST_APPEND(myd->tid, millis_before_commit, dur);
            #endif

            gMemManager.commit(tl_allocInfo);
            STATS_LIST_APPEND(myd->tid, rdset_size_at_commit, myd->readSet.size);
            STATS_LIST_APPEND(myd->tid, read_count_at_commit, myd->readCount);
            STATS_LIST_APPEND(myd->tid, aborts_before_commit, myd->attempt-1);
            myd->attempt = 0;
            // myd->snapshotAttempts = 0;
            return true;
        }

        if (!myd->readSet.validate(myd->rClock, tid)) {
            STATS_COUNTER_ADD(myd->tid, aborts_writer_failed_rdset_validation, 1);
            abortTx(myd);
        }

        STATS_LIST_APPEND(myd->tid, wrset_size_at_commit, myd->writeSet.size);
        STATS_LIST_APPEND(myd->tid, snapshot_wrset_size_at_commit, myd->snapshotWriteSet.size);

        gMemManager.commit(tl_allocInfo);

        #ifndef USE_RDTSC
        uint64_t commitTS = globals.gClock.load();
        #else
        uint64_t commitTS = rdtsc_get_read_ts();
        #endif
        myd->snapshotWriteSet.removeTBDs(myd->tid, commitTS);

        myd->writeSet.unlock(commitTS, tid);
        #ifdef TRACK_REGULAR_METRICS
        STATS_COUNTER_ADD(myd->tid, commits, 1);
        STATS_LIST_APPEND(myd->tid, rdset_size_at_commit, myd->readSet.size);
        STATS_LIST_APPEND(myd->tid, aborts_before_commit, myd->attempt-1);
        #endif

        // we will count updates as 'small' txns since it is preferable
        //  to NOT be in mode2 for updaters
        myd->consecutiveSmallTxns++;
        #ifndef TESTING_FORCE_MODE2_READS
        if (myd->consecutiveSmallTxns >= MIN_SMALL_TXNS_TO_UNSTICK_MODE2) {
            myd->forceMode2Reads = false;
        }
        #endif

        #ifdef TRACK_TOTAL_READ_TIME
        myd->totalReads += myd->readCount;
        #endif

        myd->attempt = 0;
        // myd->snapshotAttempts = 0;
        return true;
    }

    template<typename F> 
    void transaction(F&& func, int txType=TX_IS_UPDATE) {
        if (tl_opdata != nullptr) {
            func();
            return;
        }
        const int tid = ThreadRegistry::getTID();
        OpData* myd = &opDesc[tid];
        tl_opdata = myd;
        tl_allocInfo = &gMemManager.perThreadAllocInfo[tid];

        #ifdef TESTING_FORCE_MODE2_READS
        if (txType == TX_IS_READ) {
            transitionToMode2(myd);
        }
        #endif

        #ifdef TESTING_FORCE_SNAPSHOT_READS
        if (txType == TX_IS_READ) {
            txType == TX_IS_SNAPSHOT;
        }
        #endif

        // if (txType == TX_IS_READ && myd->forceMode2Reads) {
        //     myd->tx_type = TX_IS_SNAPSHOT_MODE2;
        //     // TM_TIMELINE_BLIP(tid, "mode2_start");
        //     // TM_TIMELINE_START(tid);
        // }
        // else {
        //     myd->tx_type = txType;
        // }
        myd->tx_type = txType;

        myd->forceVersioning = false;

        #ifdef DEBUG_SPECIAL
        myd->modeCounterAtStartTxn = globals.gTmModeCounter.load();
        #endif

        #ifdef TRACK_SNAPSHOT_METRICS_2
        myd->numTryVersionsDuringTxn = 0;
        myd->numFailedTryVersionsDuringTxn = 0;
        #endif

        #if defined(TRACK_TIME_BEFORE_COMMIT) || defined(TRACK_TIME_BEFORE_ABORT)
        myd->txnStartTime = std::chrono::system_clock::now();
        #endif

        setjmp(myd->env);
        myd->attempt++;
        // if (myd->tx_type == TX_IS_SNAPSHOT) {
        //     myd->snapshotAttempts++;
        // }
        backoff(myd, myd->attempt);
        beginTx(myd, tid);
        func();
        endTx(myd, tid);
        tl_opdata = nullptr;
    }

    template<typename R, typename F> static R updateTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_UPDATE); }
    template<typename R, typename F> static R readTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_READ); }
    template<typename F> static void updateTx(F&& func) { gMultiverse.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTx(F&& func) { gMultiverse.transaction(func, TX_IS_READ); }

    #ifdef ENABLE_SNAPSHOT_ISOLATION
    template<typename F> static void snapshotIsolationTx(F&& func) { gMultiverse.transaction(func, TX_IS_SNAPSHOT_ISOLATION); }
    #endif

    void printMaxAbortInfo(OpData* myd, uint64_t attempt) {
        printf("\n---------------------\n");
        // printf("Tid: %ld, tx_type %d, myMode: %d, snapshotAttempts: %ld, OP was: %d\n", myd->tid, myd->tx_type, myd->myTmMode, myd->snapshotAttempts, OP_TYPE);
        printf("---------------------\n");
        int expected = 0;
        if (debugLock.compare_exchange_strong(expected, 1)) {
            if (myd->tx_type == TX_IS_SNAPSHOT) {
                printf("TXN WAS SNAPSHOT\n");
            }
            else if (myd->tx_type == TX_IS_SNAPSHOT_MODE2) {
                printf("TXN WAS SNAPSHOT MODE 2\n");
            }
            #ifdef ENABLE_SNAPSHOT_ISOLATION
            else if (myd->tx_type == TX_IS_SNAPSHOT_ISOLATION) {
                printf("TXN WAS SNAPSHOT ISOLATION\n");
            }
            #endif
            else if (myd->tx_type == TX_IS_UPDATE) {
                printf("TXN WAS TL2 updater\n");
                if (myd->forceVersioning) {
                    printf("force-versioning is enabled\n");
                }
            }
            else {
                printf("TXN WAS TL2 reader\n");
            }
            printf("tl2 write set size = %ld\n", myd->writeSet.size);
            printf("snapshot write set size = %ld\n", myd->snapshotWriteSet.size);
            printf("read set size = %ld\n", myd->readSet.size);

            uint64_t clock = globals.gClock.load();
            printf("global tl2 clock = %ld\n", clock);
            printf("rclock clock = %ld\n", myd->rClock);
            printf("mySnapshotTs = %ld\n", myd->rClock);

            printf("myd->myTmMode = %d\n", myd->myTmMode);
            printf("myd->myTmModeCounter = %ld\n", myd->myTmModeCounter);

            printf("myd->thisTxnForcedMode2Swap = %d\n", myd->thisTxnForcedMode2Swap);

            #ifdef DEBUG_SPECIAL
            printf("modeCounterAtStartTxn = %ld\n", myd->modeCounterAtStartTxn);
            printf("modeAtStartTxn = %d\n", getCounterMode(myd->modeCounterAtStartTxn));
            printf("oldest_ts_at_last_abort = %ld\n", myd->oldest_ts_at_last_abort);
            #endif

            long counter = globals.gTmModeCounter.load();
            printf("global tm mode counter = %ld\n", counter);
            printf("global tm mode = %d\n", getCounterMode(counter));
            printf("\n");

            printf("tx_type of all threads:\n");
            for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
                printf("%d, ", opDesc[it].tx_type);
            }
            printf("\n");

            printf("tmModes of all threads:\n");
            for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
                printf("%d, ", opDesc[it].myTmMode);
            }
            printf("\n");

            printf("thisTxnForcedMode2Swap of all threads\n");
            for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
                printf("%d, ", opDesc[it].thisTxnForcedMode2Swap);
            }

            #ifdef USE_GSTATS
            GSTATS_PRINT;
            #endif
            printf("\n");

            // bgThreadShutdown = true;
            // __sync_synchronize();
            // bgThread->join();

            //intentially forcing seg fault here for debugging core dumps
            int x = *((uint64_t*)(41ull));
            printf("%d\n", x);
            exit(-1);
        }
        std::this_thread::yield();
    }

    // Backoff for a random amount of steps in the range [16, 16*attempt]. Inspired by TL2
    inline void backoff(OpData* myd, uint64_t attempt) {
        if (attempt < 3) return;
        if (attempt == MAX_ABORTS) {
            printf("MAX ABORTS REACHED\n");
            #ifdef PRINT_MAX_ABORTS_DEBUG_INFO
            printMaxAbortInfo(myd, attempt);
            #else
            exit(-1);
            #endif
        }
        myd->myrand = marsagliaXORV(myd->myrand);
        uint64_t stall = (myd->myrand & attempt) + 1;
        stall *= 16;
        std::atomic<uint64_t> iter {0};
        while (iter.load() < stall) iter.fetch_add(1);
        if (stall > 1000) std::this_thread::yield();
    }

    // template<typename R,class F> inline static R readTx(F&& func) {
    //     gMultiverse.transaction([&]() {func();}, TX_IS_READ);
    //     return R{};
    // }
    // template<typename R,class F> inline static R updateTx(F&& func) {
    //     gMultiverse.transaction([&]() {func();}, TX_IS_UPDATE);
    //     return R{};
    // }



    template <typename T, typename... Args>
    static T* tmNew(Args&&... args) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        void* ptr = gMemManager.bufferedAllocate(sizeof(T));
        assert(ptr != nullptr);
        new (ptr) T(std::forward<Args>(args)...);
        return (T*)ptr;
    }

    template<typename T>
    static void tmDelete(T* obj) {
        #ifdef DISABLE_TM_FREE
        return;
        #endif
        //
        if (obj == nullptr) return;
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not de-allocate outside a transaction\n");
            return;
        }
        obj->~T();
        gMemManager.deferFree(obj);
    }

    static void* tmMalloc(size_t size) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        return gMemManager.bufferedAllocate(size);
    }

    static void tmFree(void* ptr) {
        #ifdef DISABLE_TM_FREE
        return;
        #endif
        //
        if (ptr == nullptr) return;
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not de-allocate outside a transaction\n");
            return;
        }
        gMemManager.deferFree(ptr);
    }
};


// T should be 64 bits or less (we pad it to 64 bits but this could be changed)
template<typename T>
struct tx_field {
    static_assert(sizeof(T) <= sizeof(uint64_t));

    T data;
    // uint8_t pad[sizeof(uint64_t)-sizeof(T)];

    tx_field() {
    }

    tx_field(T initVal) { store(initVal); }

    // Casting operator
    operator T() { return load(); }
    // Casting to const
    operator T() const { return load(); }

    // Prefix increment operator: ++x
    void operator++ () { store(load()+1); }
    // Prefix decrement operator: --x
    void operator-- () { store(load()-1); }
    void operator++ (int) { store(load()+1); }
    void operator-- (int) { store(load()-1); }
    tx_field<T>& operator+= (const T& rhs) { store(load() + rhs); return *this; }
    tx_field<T>& operator-= (const T& rhs) { store(load() - rhs); return *this; }

    // Equals operator
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator == (const tx_field<Y> &rhs) { return load() == rhs; }
    // Difference operator: first downcast to T and then compare
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator != (const tx_field<Y> &rhs) { return load() != rhs; }
    // Relational operators
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator < (const tx_field<Y> &rhs) { return load() < rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator > (const tx_field<Y> &rhs) { return load() > rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator <= (const tx_field<Y> &rhs) { return load() <= rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator >= (const tx_field<Y> &rhs) { return load() >= rhs; }

    // Operator arrow ->
    T operator->() { return load(); }

    // Copy constructor
    tx_field<T>(const tx_field<T>& other) { store(other.load()); }

    // Assignment operator from a tx_field<T>
    tx_field<T>& operator=(const tx_field<T>& other) {
        store(other.load());
        return *this;
    }

    // Assignment operator from a value
    tx_field<T>& operator=(T value) {
        store(value);
        return *this;
    }

    // Operator &
    T* operator&() {
        return (T*)this;
    }

    // returns true and stores the data in _val if we versioned this address
    // returns false if we didnt version the address and need to retry
    // aborts the txn if we version the address but our initial read was not consistent
    bool tryVersionAddr(T* _val, OpData* myd, uint64_t* addr, uint64_t tableIndex, std::atomic<uint64_t>* mutex, LockTableNode* ltn) const {
        //acquire lock
        uint64_t sl = mutex->load(std::memory_order_acquire);
        while (!versioning_tryWriteLock(VERSIONING_TID, mutex, sl)) {
        // while (!tryWriteLock(myd->tid, mutex, sl)) {
            sl = mutex->load(std::memory_order_acquire);
        }

        bool wasReadConsistent = true;
        if (getTS(sl) >= myd->rClock) {
            wasReadConsistent = false;
        }

        sl = mutex->load(std::memory_order_acquire);
        assert(isLocked(sl));

        //look for this addr in the vlist table
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        if (found) {
            //unlock to the same clock that we acquired on
            //  note that the lock MUST have been unlocked since snapshots are only
            //  enabled for read-only txns in this version
            uint64_t newSL = (myd->tid << TID_SHIFT) | (getTS(sl));
            assert(isUnlocked(newSL));
            mutex->store(newSL, std::memory_order_release);
            #ifdef TRACK_SNAPSHOT_METRICS_2
            STATS_COUNTER_ADD(myd->tid, snapshot_num_failed_tryVersions, 1);
            myd->numFailedTryVersionsDuringTxn++;
            #endif
            return false;
        }

        uint64_t lastTs;
        uint64_t firstMode2Ts = globals.gFirstObservedMode2Timestamp.load(std::memory_order_relaxed);
        if (firstMode2Ts != -1) {
            lastTs = firstMode2Ts;
        }
        else {
            lastTs = getTS(sl);
        }

        T val = data;

        VersionedNode* vNode = (VersionedNode*)(gMemManager.allocate(sizeof(VersionedNode)));
        // write current version (no need to mark TBD since we havent linked this vnode into the list yet)
        vNode->ts.store(lastTs, std::memory_order_relaxed);
        // write current value - the order of this write relative to the TS write is also not relevant since the vnode is not yet reachable
        vNode->data = (uint64_t)val;
        vNode->next = nullptr;

        vtn = (VListTableNode*)(gMemManager.allocate(sizeof(VListTableNode)));
        vtn->addr = addr;
        vtn->listHead.store(vNode, std::memory_order_relaxed);
        vtn->next = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
        globals.gVlistTable[tableIndex].store(vtn, std::memory_order_relaxed);

        #ifdef COLOCATE_BLOOM_FILTER
        if (!ltn->bloom.contains(addr)) {
            ltn->bloom.insertFreshUnsafe(addr);
        }
        #else
        if (!globals.blooms[tableIndex].contains(addr)) {
            globals.blooms[tableIndex].insertFreshUnsafe(addr);
        }
        #endif

        //unlock to the same clock that we acquired on
        //  note that the lock MUST have been unlocked since snapshots are only
        //  enabled for read-only txns in this version
        uint64_t newSL = (myd->tid << TID_SHIFT) | (getTS(sl));
        assert(isUnlocked(newSL));
        mutex->store(newSL, std::memory_order_release);

        #ifdef TRACK_SNAPSHOT_METRICS_2
        STATS_COUNTER_ADD(myd->tid, snapshot_num_successful_tryVersions, 1);
        myd->numTryVersionsDuringTxn++;
        #endif
        if (wasReadConsistent) {
            *_val = val;
            return true;
        }
        else {
            STATS_COUNTER_ADD(myd->tid, aborts_snapshot_tryVersion_read_inconsistent, 1);
            abortTx(myd);
        }
    }

    FORCE_NO_INLINE T snapshotLoad() const {
        OpData* const myd = tl_opdata;
        uint64_t* addr = (uint64_t*)(&data);
        uint64_t tableIndex = hidx((size_t)addr);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];

        if (myd->myTmMode == TM_MODE_MODE2) {
            return mode2_snapshotLoad(myd, addr, tableIndex, ltn);
        }
        else {
            return defaultMode_snapshotLoad(myd, addr, tableIndex, ltn);
        }
    }

    T defaultMode_snapshotLoad(OpData* myd, uint64_t* addr, uint64_t tableIndex, LockTableNode* ltn) const {
        uint64_t retryCount = 0;
        while (retryCount < MAX_ABORTS) {
            retryCount++;
            VListTableNode* vtn = nullptr;
            bool found = false;
            T val;

            #ifdef COLOCATE_BLOOM_FILTER
            if (ltn->bloom.contains(addr)) {
            #else
            if (globals.blooms[tableIndex].contains(addr)) {
            #endif
                findAddrInVLT(myd, addr, tableIndex, vtn, found);
            }

            if (!found) {
                // version this address
                std::atomic<uint64_t>* mutex = &ltn->mutex;
                if (tryVersionAddr(&val, myd, addr, tableIndex, mutex, ltn)) {
                    return val;
                }
                else {
                    continue;
                }
            }

            VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
            uint64_t markedTs = vNode->ts.load(std::memory_order_acquire);

            while (isTBD(markedTs) && (getUnmarkedTimestamp(markedTs) <= myd->rClock)) {
                markedTs = vNode->ts.load(std::memory_order_acquire);
            }

            int vlistTraversalDepth = 0;
            while (vNode && (getUnmarkedTimestamp(markedTs) > myd->rClock) || markedTs == EMPTY_SNAPSHOT_TIMESTAMP) {
                vNode = vNode->next;
                if (!vNode) {
                    break;
                }
                markedTs = vNode->ts.load(std::memory_order_acquire);
                vlistTraversalDepth++;
            }

            if (vlistTraversalDepth > 0) {
                myd->numSnapReadsPastHead++;
            }

            if (!vNode) {
                STATS_COUNTER_ADD(myd->tid, aborts_snapshot_vlist_failed_find_ts, 1);
                abortTx(myd);
            }

            val = (T)(vNode->data);

            return val;
        }
        printf("Snapshot load stuck retrying. Existing\n");
        exit(-3);
    }

    T mode2_snapshotLoad(OpData* myd, uint64_t* addr, uint64_t tableIndex, LockTableNode* ltn) const {
        uint64_t retryCount = 0;
        uint64_t lastSL = 0;
        T lastVal = (T)0;
        bool found = false;
        #ifdef TRACK_TOTAL_READ_TIME
        uint64_t readStartTime = get_server_clock();
        #endif
        while (retryCount < MAX_SNAPSHOT_MODE2_RETRIES_BEFORE_LOCKING*10) {
            retryCount++;
            VListTableNode* vtn = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
            T val;

            #ifdef COLOCATE_BLOOM_FILTER
            if (!found && ltn->bloom.contains(addr)) {
            #else
            if (!found && globals.blooms[tableIndex].contains(addr)) {
            #endif
                // STATS_RESET_TIMER(myd->tid, mode2_vltable_search_timer_duration);
                findAddrInVLT(myd, addr, tableIndex, vtn, found);
                // auto vtnTime_us = STATS_ELAPSED_TIMER(myd->tid, mode2_vltable_search_timer_duration)/1000.0;
                // STATS_COUNTER_ADD(myd->tid, mode2_vltable_search_timer_duration_us, vtnTime_us);
            }

            if (!found) {
                std::atomic<uint64_t>* mutex = &ltn->mutex;

                //There are only 2 ways in which we could find a locked addr that is unversioned
                //  (1) The addr is locked by a writer that is about to version it
                //  (2) Lock table collision - Some other addr that maps to the same lock is being written
                //      In either case if we do 2 tries at reading this addr's vlist and lock and we find either
                //      (a) a change in the lock + unversioned or
                //      (b) no changes + unversioned or
                //      (c) versioned (obvious)
                //      Then we can return the last value read, since for (a) and (b) any writes would have versioned the addr
                uint64_t sl = mutex->load(std::memory_order_acquire);
                if (isLocked(sl)) {
                    if (retryCount > 1 && (sl != lastSL || (sl == lastSL && data == lastVal))) {
                        #ifdef TRACK_TOTAL_READ_TIME
                        uint64_t readEndTime = get_server_clock();
                        double readTime = ((readEndTime - readStartTime) /1000.0);
                        myd->totalReadTime += readTime;
                        if (myd->maxReadTime < readTime) {
                            myd->maxReadTime = readTime;
                            myd->retriesAtMaxReadTime = retryCount;
                        }
                        #endif
                        return lastVal;
                    }

                    STATS_COUNTER_ADD(myd->tid, mode2_found_locked_retry, 1);

                    lastSL = sl;
                    lastVal = data;
                    continue;
                }
                val = data;
                uint64_t sl2 = mutex->load(std::memory_order_acquire);
                if (sl2 != sl) {
                    STATS_COUNTER_ADD(myd->tid, aborts_mode2_snapshot_addr_lock_changed_after_read, 1);
                    abortTx(myd);
                }
                #ifdef TRACK_TOTAL_READ_TIME
                uint64_t readEndTime = get_server_clock();
                double readTime = ((readEndTime - readStartTime) /1000.0);
                myd->totalReadTime += readTime;
                if (myd->maxReadTime < readTime) {
                    myd->maxReadTime = readTime;
                    myd->retriesAtMaxReadTime = retryCount;
                }
                #endif
                return val;
            }

            VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
            uint64_t markedTs = vNode->ts.load(std::memory_order_acquire);

            // STATS_RESET_TIMER(myd->tid, spin_timer_duration);
            while (isTBD(markedTs) && (getUnmarkedTimestamp(markedTs) <= myd->rClock)) {
                markedTs = vNode->ts.load(std::memory_order_acquire);
            }
            // auto spinTime_us = STATS_ELAPSED_TIMER(myd->tid, spin_timer_duration)/1000.0;
            // STATS_COUNTER_ADD(myd->tid, mode2_spin_timer_duration_us, spinTime_us);

            #ifdef DEBUG_SPECIAL
            uint64_t oldest_ts = (uint64_t)(-1);
            #endif

            uint64_t vlistTraversalDepth = 1;
            uint64_t verDiff = 0;
            while (vNode && (getUnmarkedTimestamp(markedTs) > myd->rClock) || (markedTs == EMPTY_SNAPSHOT_TIMESTAMP)) {
                verDiff = getUnmarkedTimestamp(markedTs) - myd->rClock;
                vNode = vNode->next;
                if (!vNode) {
                    break;
                }
                markedTs = vNode->ts.load(std::memory_order_acquire);
                vlistTraversalDepth++;

                #ifdef DEBUG_SPECIAL
                oldest_ts = getUnmarkedTimestamp(markedTs);
                #endif
            }

            #ifdef TRACK_VLIST_TRAVERAL_DEPTH
            // uint64_t log2Index = std::bit_width(vlistTraversalDepth) - 1;
            uint64_t log2Index = get_bit_width(vlistTraversalDepth) - 1;
            myd->vlistCount[log2Index] += 1;
            #endif

            if (vlistTraversalDepth > 0) {
                myd->numSnapReadsPastHead++;
            }

            if (!vNode) {
                STATS_COUNTER_ADD(myd->tid, aborts_mode2_snapshot_vlist_failed_find_ts, 1);
                #ifdef DEBUG_SPECIAL
                myd->oldest_ts_at_last_abort = oldest_ts;
                #endif
                abortTx(myd);
            }

            val = (T)(vNode->data);

            #ifdef TRACK_TOTAL_READ_TIME
            uint64_t readEndTime = get_server_clock();
            double readTime = ((readEndTime - readStartTime) /1000.0);
            myd->totalReadTime += readTime;
            if (myd->maxReadTime < readTime) {
                myd->maxReadTime = readTime;
                myd->retriesAtMaxReadTime = retryCount;
            }
            #endif
            return val;
        }

        printf("Mode 2 snapshot load stuck\n");
        exit(-3);
    }

    FORCE_INLINE bool tryVersionedStore(OpData* myd, uint64_t* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn) {
        //check if the addr has a vlist - if it does not then we hit a false positive in the bloom filter
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        if (!found) {
            return false;
        }

        VersionedNode* head = vtn->listHead.load(std::memory_order_relaxed);

        assert(head != nullptr);

        myd->writeSet.add(addr, *addr);
        data = newVal;

        uint64_t ts = head->ts.load(std::memory_order_relaxed);
        if (isTBD(ts)) {
            //this vnode is marked TBD but we hold the lock so we must have been the one to add the TBD
           head->data = (uint64_t)newVal;
        }
        else {
            //this is the first time we are writting to this address
            VersionedNode* vNode = (VersionedNode*)gMemManager.allocate(sizeof(VersionedNode));
            vNode->next = head;
            vNode->ts.store(makeTBDSnapshotTimestamp(myd->rClock), std::memory_order_relaxed);
            vNode->data = (uint64_t)newVal;
            vtn->listHead.store(vNode, std::memory_order_relaxed);
            myd->snapshotWriteSet.add(vtn);
            gMemManager.retireWithPossibleRollback(head);
        }

        return true;
    }

    #ifdef ENABLE_SNAPSHOT_ISOLATION
    void snapshotIsolationStore(OpData* myd, uint64_t* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn) {
        std::atomic<uint64_t>* mutex = &ltn->mutex;
        uint64_t sl = mutex->load(std::memory_order_acquire);
        while (!tryWriteLock(myd->tid, mutex, sl)) {
            sl = mutex->load(std::memory_order_acquire);
        }

        if (myd->forceVersioning) {
            forceVersionedStore(myd, addr, newVal, tableIndex, ltn, getTS(sl));
            return;
        }

        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        if (!found) {
            myd->writeSet.add(addr, *addr);
            data = newVal;
            return;
        }

        VersionedNode* head = vtn->listHead.load(std::memory_order_relaxed);

        assert(head != nullptr);

        myd->writeSet.add(addr, *addr);
        data = newVal;

        uint64_t ts = head->ts.load(std::memory_order_relaxed);
        if (isTBD(ts)) {
            //this vnode is marked TBD but we hold the lock so we must have been the one to add the TBD
           head->data = (uint64_t)newVal;
        }
        else {
            //this is the first time we are writting to this address
            VersionedNode* vNode = (VersionedNode*)gMemManager.allocate(sizeof(VersionedNode));
            vNode->next = head;
            vNode->ts.store(makeTBDSnapshotTimestamp(myd->rClock), std::memory_order_relaxed);
            vNode->data = (uint64_t)newVal;
            vtn->listHead.store(vNode, std::memory_order_relaxed);
            myd->snapshotWriteSet.add(vtn);
            gMemManager.retireWithPossibleRollback(head);
        }
    }
    #endif

    void forceVersionedStore(OpData* myd, uint64_t* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn, uint64_t lastLockTs) {
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        VersionedNode* head;
        if (!found) {
            VersionedNode* vNode = (VersionedNode*)(gMemManager.allocate(sizeof(VersionedNode)));
            // This new version is guaranteed to be added, it will not be removed on abort
            //  if we are in mode 2 we attempt to use the gFirstObservedMode2Timestamp
            //  otherwise we version it to be the last version assocaited with this lock table entry
            // TODO:: we could check this threads tmMode to see if we are in mode 2 or intermediate 2 to sometimes avoid reading the shared global
            uint64_t firstMode2Ts = globals.gFirstObservedMode2Timestamp.load(std::memory_order_relaxed);
            if (firstMode2Ts != -1) {
                vNode->ts.store(firstMode2Ts, std::memory_order_relaxed);
            }
            else {
                vNode->ts.store(lastLockTs, std::memory_order_relaxed);
            }
            vNode->data = (uint64_t)data;
            vNode->next = nullptr;

            vtn = (VListTableNode*)(gMemManager.allocate(sizeof(VListTableNode)));
            vtn->addr = addr;
            vtn->listHead.store(vNode, std::memory_order_relaxed);

            vtn->next = globals.gVlistTable[tableIndex].load(std::memory_order_acquire);
            globals.gVlistTable[tableIndex].store(vtn, std::memory_order_release);

            STATS_COUNTER_ADD(tid, snapshot_num_writer_successful_tryVersions, 1);
            head = vNode;
        }
        else {
            head = vtn->listHead.load(std::memory_order_relaxed);
        }

        #ifdef COLOCATE_BLOOM_FILTER
        if (!ltn->bloom.contains(addr)) {
            ltn->bloom.insertFreshUnsafe(addr);
        }
        #else
        if (!globals.blooms[tableIndex].contains(addr)) {
            globals.blooms[tableIndex].insertFreshUnsafe(addr);
        }
        #endif

        assert(vtn != nullptr);
        assert(head != nullptr);

        myd->writeSet.add(addr, *addr);
        data = newVal;

        uint64_t ts = head->ts.load(std::memory_order_relaxed);
        if (isTBD(ts)) {
            //this vnode is marked TBD but we hold the lock so we must have been the one to add the TBD
            head->data = (uint64_t)newVal;
        }
        else {
            //this is the first time we are writting to this address
            VersionedNode* vNode = (VersionedNode*)gMemManager.allocate(sizeof(VersionedNode));
            vNode->next = head;
            vNode->ts.store(makeTBDSnapshotTimestamp(myd->rClock), std::memory_order_relaxed);
            vNode->data = (uint64_t)newVal;
            vtn->listHead.store(vNode, std::memory_order_relaxed);
            myd->snapshotWriteSet.add(vtn);
            gMemManager.retireWithPossibleRollback(head);
        }

        return;
    }

    FORCE_INLINE void store(T newVal) {
        OpData* const myd = tl_opdata;

        uint64_t* addr = (uint64_t*)&data;

        uint64_t tableIndex = hidx((size_t)addr);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];
        assert(tableIndex < NUM_LOCKS);
        std::atomic<uint64_t>* mutex = &ltn->mutex;

        #ifdef ENABLE_SNAPSHOT_ISOLATION
        if (myd->tx_type == TX_IS_SNAPSHOT_ISOLATION) {
            snapshotIsolationStore(myd, addr, newVal, tableIndex, ltn);
            return;
        }
        #endif

        uint64_t sl;
        while (true) {
            sl = mutex->load(std::memory_order_acquire);
            if (isInprogressVersioning(sl)) {
                continue;
            }
            if (!isWriteConsistent(myd->rClock, myd->tid, sl)) {
                STATS_COUNTER_ADD(myd->tid, aborts_tl2_store_write_inconsistent, 1);
                abortTx(myd);
            }
            break;
        }

        if (!tryWriteLock(myd->tid, mutex, sl)) {
            STATS_COUNTER_ADD(myd->tid, aborts_tl2_store_failed_lock_acquire, 1);
            abortTx(myd);
        }

        if (myd->forceVersioning) {
            forceVersionedStore(myd, addr, newVal, tableIndex, ltn, getTS(sl));
            return;
        }

        #ifdef COLOCATE_BLOOM_FILTER
        if (__glibc_unlikely(ltn->bloom.contains(addr))) {
        #else
        if (__glibc_unlikely(globals.blooms[tableIndex].contains(addr))) {
        #endif
            if (tryVersionedStore(myd, addr, newVal, tableIndex, ltn)) {
                data = newVal;
                return;
            }
        }
        myd->writeSet.add(addr, *addr);
        data = newVal;
    }

    FORCE_INLINE T load_tl2() const {
        OpData* const myd = tl_opdata;
        myd->readCount++;

        uint64_t* addr = (uint64_t*)(&data);
        uint64_t tableIndex = hidx((size_t)addr);
        assert(tableIndex < NUM_LOCKS);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];

        T val = data;
        std::atomic<uint64_t>* mutex = &ltn->mutex;

        while (true) {
            uint64_t sl = mutex->load(std::memory_order_acquire);
            if (!isReadConsistent(myd->rClock, myd->tid, sl)) {
                if (isInprogressVersioning(sl)) {
                    continue;
                }
                STATS_COUNTER_ADD(myd->tid, aborts_tl2_load_read_inconsistent, 1);
                abortTx(myd);
            }
            break;
        }

        if (myd->tx_type == TX_IS_UPDATE) {
            myd->readSet.add(mutex);
        }
        return val;
    }

    FORCE_INLINE T load() const {
        OpData* const myd = tl_opdata;
        myd->readCount++;

        if (__glibc_unlikely(myd->tx_type & TX_IS_SNAPSHOT)) {
            return snapshotLoad();
        }
        return load_tl2();

        // uint64_t* addr = (uint64_t*)(&data);
        // uint64_t tableIndex = hidx((size_t)addr);
        // assert(tableIndex < NUM_LOCKS);
        // LockTableNode* ltn = &globals.gHashLock[tableIndex];


        // T val = data;
        // std::atomic<uint64_t>* mutex = &ltn->mutex;
        // uint64_t sl = mutex->load(std::memory_order_acquire);

        // // while (isInprogressVersioning(sl)) {
        // //     sl = mutex->load(std::memory_order_acquire);
        // // }

        // while (true) {
        //     if (!isReadConsistent(myd->rClock, myd->tid, sl)) {
        //         if (isInprogressVersioning(sl)) {
        //             continue;
        //         }
        //         STATS_COUNTER_ADD(myd->tid, aborts_tl2_load_read_inconsistent, 1);
        //         abortTx(myd);
        //     }
        // }

        // if (myd->tx_type == TX_IS_UPDATE) {
        //     myd->readSet.add(mutex);
        // }
        // return val;
    }
};


//
// Wrapper methods to the global TM instance. The user should use these:
//
// template<typename R, typename F> static R updateTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_UPDATE); }
// template<typename R, typename F> static R readTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_READ); }
template<typename F> static void updateTx(F&& func) { gMultiverse.transaction(func, TX_IS_UPDATE); }
template<typename F> static void readTx(F&& func) { gMultiverse.transaction(func, TX_IS_READ); }
template<typename T, typename... Args> T* tmNew(Args&&... args) { return Multiverse::tmNew<T>(args...); }
template<typename T> void tmDelete(T* obj) { Multiverse::tmDelete<T>(obj); }
static void* tmMalloc(size_t size) { return Multiverse::tmMalloc(size); }
static void tmFree(void* obj) { Multiverse::tmFree(obj); }

// The following ifdef (and corresponding endif) are needed when using setbench
// #ifdef MICROBENCH
//
// Place these in a .cpp if you include this header from multiple files (compilation units)
//
ThreadRegistry gThreadRegistry {};

Globals globals {};
// Globals* globals {};

Multiverse gMultiverse {};

MemoryManager gMemManager {};

alignas(128) thread_local AllocInfo* tl_allocInfo {nullptr};

// Thread-local data of the current ongoing transaction
alignas(128) thread_local OpData* tl_opdata {nullptr};
// This is where every thread stores the tid it has been assigned when it calls getTID() for the first time.
// When the thread dies, the destructor of ThreadCheckInCheckOut will be called and de-register the thread.
thread_local ThreadCheckInCheckOut tl_tcico {};
// Helper function for thread de-registration
void thread_registry_deregister_thread(const int tid) {
    gThreadRegistry.deregister_thread(tid);
}
void thread_registry_deregister_thread() {
    const int tid = ThreadRegistry::getTID();
    gThreadRegistry.deregister_thread(tid);
}

[[noreturn]] void abortTx(OpData* myd) {
    #ifdef TRACK_TOTAL_READ_TIME
    myd->totalReads += myd->readCount;
    #endif

    #ifdef TRACK_TIME_BEFORE_ABORT
    std::chrono::time_point<std::chrono::system_clock> now = std::chrono::system_clock::now();
    int64_t dur = std::chrono::duration_cast<std::chrono::milliseconds>(now - myd->txnStartTime).count();
    STATS_LIST_APPEND(myd->tid, millis_before_abort, dur);
    #endif

    myd->writeSet.rollback(myd->tid);
    myd->snapshotWriteSet.rollback(myd->tid);

    #ifndef USE_RDTSC
    // uint64_t nextClock = globals.gClock.fetch_add(1);

    uint64_t nextClock;
    if (globals.gClock.compare_exchange_strong(myd->rClock, myd->rClock+1)) {
        nextClock = myd->rClock+1;
    }
    else {
        nextClock = globals.gClock.load(std::memory_order_relaxed);
    }
    #else
    uint64_t nextClock = rdtsc_get_abort_ts();
    #endif

    STATS_LIST_APPEND(myd->tid, rdset_size_at_abort, myd->readSet.size);
    STATS_LIST_APPEND(myd->tid, wrset_size_at_abort, myd->writeSet.size);
    STATS_LIST_APPEND(myd->tid, snapshot_wrset_size_at_abort, myd->snapshotWriteSet.size);

    gMemManager.abort(myd->tid);

    // Unlock with the next sequence
    myd->writeSet.unlock(nextClock, myd->tid);
    #ifdef TRACK_REGULAR_METRICS
    STATS_COUNTER_ADD(myd->tid, aborts, 1);
    #endif

    #ifdef TRACK_SNAPSHOT_METRICS
    if (myd->tx_type == TX_IS_SNAPSHOT) {
        STATS_COUNTER_ADD(myd->tid, aborts_snapshot, 1);
        #ifdef TRACK_SNAPSHOT_METRICS_2
        STATS_LIST_APPEND(myd->tid, snapshot_num_successful_tryVersions_per_txn, myd->numTryVersionsDuringTxn);
        STATS_LIST_APPEND(myd->tid, snapshot_num_failed_tryVersions_per_txn, myd->numFailedTryVersionsDuringTxn);
        #endif
        STATS_LIST_APPEND(myd->tid, snapshot_read_count_at_abort, myd->readCount);
    }
    else if (myd->tx_type == TX_IS_READ) {
        STATS_LIST_APPEND(myd->tid, read_count_at_abort, myd->readCount);
    }
    #endif

    #ifndef DISABLE_MODE2_SNAPSHOTS
    if (myd->tx_type == TX_IS_SNAPSHOT_MODE2 && myd->myTmMode > TM_MODE_INTERMEDIATE1) {
        // TM_TIMELINE_BLIP(tid, "mode2_abort");
        STATS_COUNTER_ADD(tid, aborts_mode2_snapshot, 1);
        STATS_LIST_APPEND(myd->tid, snapshot_read_count_at_abort, myd->readCount);
    }
    #endif

    if (myd->tx_type == TX_IS_READ) {
        uint64_t maxRdSetSize = MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.load(std::memory_order_relaxed);
        if (maxRdSetSize != 0 && myd->attempt >= RETRIES_BEFORE_CHECKING_READ_SET_SIZE_FOR_MODE2 && myd->readCount >= maxRdSetSize) {
            #ifndef DISABLE_MODE2_SNAPSHOTS
            transitionToMode2(myd);
            #endif
        }
        else if(myd->attempt > RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE && !myd->forceMode2Reads) {
            myd->tx_type = TX_IS_SNAPSHOT;
            STATS_COUNTER_ADD(myd->tid, snapshot_starts, 1);
        }
        else if(myd->attempt > RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE && myd->forceMode2Reads) {
            #ifndef DISABLE_MODE2_SNAPSHOTS
            transitionToMode2(myd);
            STATS_COUNTER_ADD(myd->tid, snapshot_starts, 1);
            #endif
        }
        STATS_COUNTER_ADD(tid, aborts_read_only_txns, 1);
    }
    #ifndef DISABLE_MODE2_SNAPSHOTS
    else if (myd->tx_type == TX_IS_SNAPSHOT) {
        uint64_t maxRdSetSize = MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.load(std::memory_order_relaxed);
        if (myd->attempt > RETRIES_BEFORE_SNAPSHOT_MODE2 || (myd->attempt >= RETRIES_BEFORE_CHECKING_READ_SET_SIZE_FOR_MODE2 && myd->readCount >= maxRdSetSize) || myd->forceMode2Reads) {
            transitionToMode2(myd);
        }
    }
    #endif

    std::longjmp(myd->env, 1);
}
// #endif // MICROBENCH

}
