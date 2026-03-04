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
#include <csetjmp>      // Needed by sigjmp_buf
#include <cstdlib>      // Needed by exit()
#include <chrono>
#include <vector>

#include "tm_common/stats.h"
#include "tm_common/tm_debug.h"
#include "tm_common/tm_timelines.h"
#include "bloom_filter.h"
#include "plaf.h"

std::atomic<int> debugLock {0};

// #define TRACK_OP_TYPE 1
#ifdef TRACK_OP_TYPE
thread_local int OP_TYPE;
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


/*
 *******************************************
 * OPTIONAL POTENTIAL OPTIMIZATIONS
 *******************************************
 *******************************************
*/

// This macro enables colocating bloom filters with the versioned locks
// #define COLOCATE_BLOOM_FILTER 1

// This macro enables unsafe stores when inside of a constructor - this includes any stores that are performed as a result of functions or classes used in constructors
#define ALLOW_UNSAFE_STORES_IN_CONSTRUCTOR 1

/*
 *******************************************
 * Tunable parameters
 *******************************************
 *******************************************
*/



// max aborts for update txns + an unused fail-safe for mode 1 versioned transactions
static const uint64_t MAX_ABORTS = 10000;

static const int MIN_RD_SET_SIZE_BEFORE_UPDATING_THRESHOLD = 256;

static const uint64_t RETRIES_BEFORE_CHECKING_READ_SET_SIZE_FOR_MODE2 = 16;
static const uint64_t RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE = 100; //snapshot readers version
static const uint64_t RETRIES_BEFORE_SNAPSHOT_MODE2 = 128; //tm writers version


static std::atomic<uint64_t> MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE {0}; // If a snapshot aborts with read set size greater than this constant then we force it to mode 2 immediately

static const uint64_t MAX_SNAPSHOT_MODE2_RETRIES_BEFORE_LOCKING = 1024*1024ULL;

static const int MIN_SMALL_TXNS_TO_UNSTICK_MODE2 = 10;

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

inline __attribute__((always_inline)) static uint64_t hidx(size_t addr) {
    return ((addr >> 6) & (NUM_LOCKS-1));  // Thanks to Pedro Ramalhete for suggesting this improved mapping function
}

static const int TID_SHIFT = 53;
static const int LOCK_SHIFT = 63;
static_assert((1<<(LOCK_SHIFT-TID_SHIFT)) >= MAX_THREADS_POW2);

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
    PAD64;
};

struct padded_atomic_u64 {
    std::atomic<uint64_t> data;
    PAD64;
};

struct padded_u64 {
    uint64_t data;
    PAD64;
};

struct VersionedNode {
    std::atomic<uint64_t> ts;
    uint64_t data;          //constant after allocation/init
    VersionedNode* next;    //constant after allocation/init
};

struct VListTableNode {
    void* addr;         //constant after allocation/init
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
    std::atomic<long> gTmModeCounter {0};
    padded_atomic_long* gThreadModeAndTxType; //this is heap allocated by the bg thread to ensure that it is local to the bg threads numa node
    padded_atomic_long* gThreadMode2Sticky; //this is heap allocated by the bg thread to ensure that it is local to the bg threads numa node
    PAD;
    std::atomic<long> gFirstObservedMode2Timestamp {0};
    PAD;
    LockTableNode* gHashLock;
    PAD;
#ifndef COLOCATE_BLOOM_FILTER
    ns_multiverse::BloomFilter64Volatile* blooms;
    PAD;
#endif
    std::atomic<VListTableNode*>* gVlistTable;
    PAD;
    padded_atomic_u64 gThreadSnapTsUpdatesBeforeCommit[MAX_THREADS_POW2];
    PAD;

    Globals() {
        gHashLock = new LockTableNode[NUM_LOCKS];
        blooms = new ns_multiverse::BloomFilter64Volatile[NUM_LOCKS];
        gVlistTable = new std::atomic<VListTableNode*>[NUM_LOCKS];
    }
};

extern Globals globals;


static const int ALLOC_BUFFER_INIT_CAPACITY = 512;
static const uint64_t EBR_RETRY_THRESHOLD = 10 * REGISTRY_MAX_THREADS;
static const double milliseconds_between_epoch_updates = 20.0;

struct AllocInfo {
    int tid;
    long currEpoch;
    uint64_t ClearBagRetries;
    PAD;

    std::vector<void*> mallocBuffer;
    std::vector<void*> freeBuffer;

    std::vector<void*> currReclaimBuffer;
    std::vector<void*> prevReclaimBuffer;

    AllocInfo() {
        mallocBuffer.reserve(ALLOC_BUFFER_INIT_CAPACITY);
        freeBuffer.reserve(ALLOC_BUFFER_INIT_CAPACITY);
        currReclaimBuffer.reserve(ALLOC_BUFFER_INIT_CAPACITY);
        prevReclaimBuffer.reserve(ALLOC_BUFFER_INIT_CAPACITY);
    }

    void commit_and_clear_free_buffer() {
        currReclaimBuffer.insert(currReclaimBuffer.end(),
                std::make_move_iterator(freeBuffer.begin()),
                std::make_move_iterator(freeBuffer.end())
        );
        freeBuffer.clear();
    }

    void reclaim_and_clear_mallocBufferContents() {
        for (void* &ptr : mallocBuffer) {
            free(ptr);
        }
        mallocBuffer.clear();
    }

    void reclaim_and_clear_prevReclaimBufferContents() {
        for (void* &ptr : prevReclaimBuffer) {
            free(ptr);
        }
        prevReclaimBuffer.clear();
    }

    void reset() {
        mallocBuffer.clear();
        freeBuffer.clear();
    }
};

alignas(128) extern thread_local AllocInfo* tl_allocInfo;

class MemoryManager;
extern MemoryManager gMemManager;

class MemoryManager {
private:
    void tryAdvanceGlobalEpoch() {
        long currentEpoch = gEpoch.load();
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            long otherEpoch = gThreadEpochs[i].data.load();
            if (otherEpoch != -1 && otherEpoch < currentEpoch) {
                return;
            }
        }

        //all threads are at or beyond current epoch
        gEpoch.compare_exchange_strong(currentEpoch, currentEpoch+1);
    }

public:
    alignas(128) std::atomic<long> gEpoch {0};
    padded_atomic_long gThreadEpochs[REGISTRY_MAX_THREADS];
    AllocInfo perThreadAllocInfo[REGISTRY_MAX_THREADS];
    PAD;



    MemoryManager() {
        for (int i = 0; i < REGISTRY_MAX_THREADS; i++) {
            perThreadAllocInfo[i].tid = i;
            perThreadAllocInfo[i].currEpoch = 0;
            perThreadAllocInfo[i].ClearBagRetries = 0;
            gThreadEpochs[i].data.store(-1, std::memory_order_relaxed);
        }
    }

    inline void announceEpoch(uint64_t tid) {
        long currentEpoch = gEpoch.load();
        gThreadEpochs[tid].data = currentEpoch;
    }

    inline void unanounceEpoch(uint64_t tid) {
        gThreadEpochs[tid].data.store(-1, std::memory_order_relaxed);
    }

    inline void forceClearBags() {
        while (!tl_allocInfo->prevReclaimBuffer.empty() || !tl_allocInfo->freeBuffer.empty()) {
            announceEpoch(tl_allocInfo->tid);
            commit();
        }
    }

    inline void commit() {
        tl_allocInfo->commit_and_clear_free_buffer();

        if (tl_allocInfo->currEpoch + 1 < gEpoch.load()) {
            tl_allocInfo->reclaim_and_clear_prevReclaimBufferContents();
            std::swap(tl_allocInfo->prevReclaimBuffer, tl_allocInfo->freeBuffer);
            tl_allocInfo->currEpoch = gEpoch.load();
        }
        tl_allocInfo->ClearBagRetries++;
        if (tl_allocInfo->ClearBagRetries == EBR_RETRY_THRESHOLD) {
            tl_allocInfo->ClearBagRetries = 0;
            tryAdvanceGlobalEpoch();
        }
    }

    inline void abort() {
        assert(tl_allocInfo);
        tl_allocInfo->reclaim_and_clear_mallocBufferContents();
    }

    inline void* buffered_malloc(size_t size) {
        assert(tl_allocInfo);
        void* ptr = malloc(size);
        tl_allocInfo->mallocBuffer.push_back(ptr);
        return ptr;
    }

    inline void deferredFree_with_possible_rollback(void* ptr) {
        assert(ptr != nullptr);
        tl_allocInfo->freeBuffer.push_back(ptr);
    }

    inline void deferredFree_irrevocable(void* ptr) {
        assert(ptr != nullptr);
        tl_allocInfo->currReclaimBuffer.push_back(ptr);
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

struct AppendLog_StandardWriteSet {
    static const int64_t MAX_ENTRIES = 7*1024;

    struct LogEntry {
        void*       addr;        
        uint64_t    oldValue;
        size_t      numBytes;
    };

    int64_t       size {0};
    LogEntry      entries[MAX_ENTRIES];
    AppendLog_StandardWriteSet*    next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    inline void add(void* addr, uint64_t oldValue, size_t numBytes) {
        if (size != MAX_ENTRIES) {
            entries[size].addr = addr;
            entries[size].oldValue = oldValue;
            entries[size].numBytes = numBytes;
            size++;
        } else {
            if (next == nullptr) {
                next = new AppendLog_StandardWriteSet();
            }
            next->add(addr, oldValue, numBytes);
        }
    }

    void rollback(uint64_t tid) {
        if (size == 0) return;
        if (next != nullptr) {
            next->rollback(tid);
        }
        for (int64_t i = size-1; i >= 0; i--) {
            // *(entries[i].addr) = entries[i].oldValue;
            std::memcpy(entries[i].addr, &entries[i].oldValue, entries[i].numBytes);
        }
    }

    inline void unlock(uint64_t nextClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            void* addr = entries[i].addr;
            std::atomic<uint64_t>* mutex = &globals.gHashLock[hidx((size_t)addr)].mutex;
            uint64_t sl = mutex->load();
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


struct AppendLog_StandardWriteSet_SnapshotWriteSet {
    // static const int64_t MAX_ENTRIES = 512;
    static const int64_t MAX_ENTRIES = 756;

    struct LogEntry {
        VListTableNode* vtn;
    };


    int64_t             size {0};
    LogEntry            entries[MAX_ENTRIES];
    AppendLog_StandardWriteSet_SnapshotWriteSet*         next {nullptr};

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
                next = new AppendLog_StandardWriteSet_SnapshotWriteSet();
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
            gMemManager.deferredFree_irrevocable(vNode);
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
    alignas(64) uint64_t            rClock {0};
    uint64_t                        tid;
    txn_type                        tx_type {TX_IS_NONE};
    ReadSet                         readSet;
    AppendLog_StandardWriteSet      writeSet;
    uint64_t                        myrand;
    std::jmp_buf                    env;
    uint64_t                        attempt {0};

    long            myTmModeCounter {0};
    int             readCount {0};
    #ifdef ALLOW_UNSAFE_STORES_IN_CONSTRUCTOR
    int             constructorCount {0};
    #endif

    uint64_t        snapshotStartTs {0};

    bool            forceVersioning {false};
    bool            forceMode2Reads {false};
    int             smallTxnReadCount {0};
    int             consecutiveSmallTxns {0};


    AppendLog_StandardWriteSet_SnapshotWriteSet      snapshotWriteSet;

    bool            thisTxnForcedMode2Swap {false};
    int             readCountAtFirstMode2Commit {0};
    int             numSnapReadsPastHead {0};

    PAD;
};

inline FORCE_INLINE static void findAddrInVLT(OpData* myd, void* addr, uint64_t tableIndex, VListTableNode* &retVtn, bool &retBool) {
    assert(tableIndex >= 0 && tableIndex < NUM_LOCKS);
    VListTableNode* vtn = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
    bool found = false;
    while (vtn) {
        if (vtn->addr == addr) {
            found = true;
            break;
        }
        vtn = vtn->next;
    }
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
    myd->myTmModeCounter = gTmModeCounter;

    if (currentTmMode != TM_MODE_DEFAULT) {
        if (currentTmMode == TM_MODE_INTERMEDIATE2 && myd->tx_type == TX_IS_SNAPSHOT_MODE2) {
            myd->tx_type = TX_IS_SNAPSHOT;
        }
        else if (currentTmMode == TM_MODE_MODE2 && myd->tx_type == TX_IS_SNAPSHOT) {
            myd->tx_type = TX_IS_SNAPSHOT_MODE2;
        }
        //if we are in mode 2 or transitioning writers must version
        myd->forceVersioning = true;
    }

    long tmModeAndTxType = combineTmModeAndTxType(gTmModeCounter, myd->tx_type);
    // globals.gThreadModeAndTxType[myd->tid].data.exchange(tmModeAndTxType);
    globals.gThreadModeAndTxType[myd->tid].data.store(tmModeAndTxType, std::memory_order_relaxed);

    //TODO:: do we need to do this every begin? Probably better to just do it when myd->forceMode2Reads changes
    globals.gThreadMode2Sticky[myd->tid].data.store(myd->forceMode2Reads, std::memory_order_relaxed);
}

static long transitionTmMode(const int tid, long lastSeenTmModeCounter, int desiredTmMode) {
    if (getCounterMode(lastSeenTmModeCounter) == desiredTmMode) {
        return -1;
    }
    long newTmTmModeCounter = (((lastSeenTmModeCounter >> TM_MODE_BIT_SHIFT) + 1) << TM_MODE_BIT_SHIFT) | (desiredTmMode);
    if (globals.gTmModeCounter.compare_exchange_strong(lastSeenTmModeCounter, newTmTmModeCounter)) {
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
        gMemManager.deferredFree_irrevocable(vNode);
        vtn_tmp = vtn;
        vtn = vtn->next;
        gMemManager.deferredFree_irrevocable(vtn_tmp);
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
            DEBUG_PRINT_TM_MODES("TM mode = %d\n", getCounterMode(tmModeCounter));
            //wait for default mode writers to drain
            DEBUG_PRINT_TM_MODES("Intermediate1: waiting for default updaters\n");
            waitForDefaultModeWriters(tmModeCounter);

            //transition to mode 2
            tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_MODE2);
            uint64_t currGTimestamp = globals.gClock.load();
            globals.gFirstObservedMode2Timestamp.store(currGTimestamp, std::memory_order_relaxed);
            DEBUG_PRINT_TM("mode_switch_to_mode2\n");

            //wait until we observe no ongoing mode 2 snapshots
            waitForStickyMode2(&myrand);

            //transition to intermediate 2
            tmModeCounter = transitionTmMode(tid, tmModeCounter, TM_MODE_INTERMEDIATE2);
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
                gMemManager.commit();
                numUnversions++;
            }
        }
        numPasses++;
    }
    #endif

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
    if (getCounterMode(myd->myTmModeCounter) == TM_MODE_DEFAULT) {
        if (-1 != transitionTmMode(myd->tid, myd->myTmModeCounter, TM_MODE_INTERMEDIATE1)) {
            DEBUG_PRINT_TM("transitioned_to_intermediate1\n");
            return true;
        }
    }
    return false;
}


class Multiverse;
extern Multiverse gMultiverse;

class Multiverse {
private:
    void snapshotRead_onCommit(OpData* myd, const int tid) {
        #ifndef USE_RDTSC
        uint64_t gTs = globals.gClock.load(std::memory_order_relaxed);
        #else
        uint64_t gTs = rdtsc_get_read_ts_relaxed();
        #endif
        globals.gThreadSnapTsUpdatesBeforeCommit[tid].data = gTs - myd->snapshotStartTs;
        myd->snapshotStartTs = 0;

        if (myd->tx_type == TX_IS_SNAPSHOT_MODE2) {
            uint64_t maxRdSetSize = MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.load(std::memory_order_relaxed);
            if (myd->readCount >= MIN_RD_SET_SIZE_BEFORE_UPDATING_THRESHOLD && (maxRdSetSize == 0 || myd->readCount < maxRdSetSize)) {
                MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.exchange(myd->readCount, std::memory_order_relaxed);
                // printf("MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE=%d\n", myd->readCount);
            }

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
        }
    }

    void printMaxAbortInfo(OpData* myd, uint64_t attempt) {
        printf("\n---------------------\n");
        // printf("Tid: %ld, tx_type %d, myMode: %d, OP was: %d\n", myd->tid, myd->tx_type, getCounterMode(myd->myTmModeCounter), OP_TYPE);
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

            printf("myd->myTmModeCounter = %ld\n", myd->myTmModeCounter);
            printf("myd->tmMode = %d\n", getCounterMode(myd->myTmModeCounter));

            printf("myd->thisTxnForcedMode2Swap = %d\n", myd->thisTxnForcedMode2Swap);

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
                printf("%d, ", getCounterMode(opDesc[it].myTmModeCounter));
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

public:
    alignas(128) OpData* opDesc;
    alignas(128) std::atomic<int> debugLock {0};
    alignas(128) std::thread* bgThread;

    Multiverse() {
        printf("sizeof(OpData)=%ld\n", sizeof(OpData));
        opDesc = new OpData[REGISTRY_MAX_THREADS];
        globals.gFirstObservedMode2Timestamp.store((uint64_t)(-1));
        for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
            opDesc[it].tid = it;
            opDesc[it].myrand = (it+1)*12345678901234567ULL;
            // globals.gThreadEpochs[it].data.store(-1, std::memory_order_relaxed);
            globals.gThreadSnapTsUpdatesBeforeCommit[it].data.store(0, std::memory_order_relaxed);
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
    }

    ~Multiverse() {
        bgThreadShutdown = true;
        __sync_synchronize();
        bgThread->join();

        // printf("\ngEpoch=%ld\n", globals.gEpoch.load());
        printf("gClock=%ld\n\n", globals.gClock.load());        

        printf("num_unversions=%ld\n", numUnversions);
        printf("last_avgTsDelta=%ld\n\n", last_avgTsDelta);

        printf("global_tm_mode=%d\n", getCounterMode(globals.gTmModeCounter.load()));
        printf("global_tm_counter=%ld\n", getUnmarkedTmModeCounter(globals.gTmModeCounter.load()));
        uint64_t numTransitions = getUnmarkedTmModeCounter(globals.gTmModeCounter.load()) / 4;
        printf("num_mode_changes=%ld\n", numTransitions);
        printf("\n\n");


        #ifdef DEBUG_TM_MODES
        printf("WARNING: DEBUG_TM_MODES is enabled\n");
        printf("\n\n");
        #endif

        delete[] opDesc;


        #ifdef NO_OPTIMIZE_IS_ON
        printf("\n");
        printf("\n");
        printf("WARNING: THIS BINARY WAS COMPILED WITH NO OPTMIZATIONS. This is not appropriate for experiment results.\n");
        printf("NO_OPTMIZATIONS_IS_ON=1\n");
        printf("\n");
        printf("\n");
        #endif
    }

    inline void beginTx(OpData* myd, const int tid) {
        gMemManager.announceEpoch(myd->tid);
        announceThreadTmMode(myd);

        #ifndef USE_RDTSC
        myd->rClock = globals.gClock.load();
        #else
        myd->rClock = rdtsc_get_read_ts();
        #endif
        if (myd->tx_type & TX_IS_SNAPSHOT) {
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
        #ifdef ALLOW_UNSAFE_STORES_IN_CONSTRUCTOR
        myd->constructorCount = 0;
        #endif
    }

    inline bool endTx(OpData* myd, const int tid) {
        std::atomic_thread_fence(std::memory_order_seq_cst);
        if (myd->writeSet.size == 0 && myd->snapshotWriteSet.size == 0) {
            if (myd->tx_type & TX_IS_SNAPSHOT) {
                snapshotRead_onCommit(myd, tid);
            }
            // gMemManager.commit();
            myd->attempt = 0;
            return true;
        }

        if (!myd->readSet.validate(myd->rClock, tid)) {
            abortTx(myd);
        }

        #ifndef USE_RDTSC
        uint64_t commitTS = globals.gClock.load();
        #else
        uint64_t commitTS = rdtsc_get_read_ts();
        #endif
        myd->snapshotWriteSet.removeTBDs(tid, commitTS);

        myd->writeSet.unlock(commitTS, tid);

        // we will count updates as 'small' txns since it is preferable
        //  to NOT be in mode2 for updaters
        myd->consecutiveSmallTxns++;
        #ifndef TESTING_FORCE_MODE2_READS
        if (myd->consecutiveSmallTxns >= MIN_SMALL_TXNS_TO_UNSTICK_MODE2) {
            myd->forceMode2Reads = false;
        }
        #endif

        gMemManager.commit();

        myd->attempt = 0;
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

        myd->tx_type = txType;
        myd->forceVersioning = false;

        setjmp(myd->env);
        myd->attempt++;
        backoff(myd, myd->attempt);
        beginTx(myd, tid);
        func();
        endTx(myd, tid);
        tl_opdata = nullptr;
    }

    //template<typename R, typename F> static R updateTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_UPDATE); }
    //template<typename R, typename F> static R readTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_READ); }
    template<typename F> static void updateTx(F&& func) { gMultiverse.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTx(F&& func) { gMultiverse.transaction(func, TX_IS_READ); }

    #ifdef ENABLE_SNAPSHOT_ISOLATION
    template<typename F> static void snapshotIsolationTx(F&& func) { gMultiverse.transaction(func, TX_IS_SNAPSHOT_ISOLATION); }
    #endif

    template<typename R,class F> inline static R readTx(F&& func) {
        gMultiverse.transaction([&]() {func();}, TX_IS_READ);
        return R{};
    }
    template<typename R,class F> inline static R updateTx(F&& func) {
        gMultiverse.transaction([&]() {func();}, TX_IS_UPDATE);
        return R{};
    }



    template <typename T, typename... Args>
    static T* tmNew(Args&&... args) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        void* ptr = gMemManager.buffered_malloc(sizeof(T));
        assert(ptr != nullptr);
        #ifdef ALLOW_UNSAFE_STORES_IN_CONSTRUCTOR
        tl_opdata->constructorCount++;
        new (ptr) T(std::forward<Args>(args)...);
        tl_opdata->constructorCount--;
        #else 
        new (ptr) T(std::forward<Args>(args)...);
        #endif
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
        gMemManager.deferredFree_with_possible_rollback(obj);
    }

    static void* tmMalloc(size_t size) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        return gMemManager.buffered_malloc(size);
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
        gMemManager.deferredFree_with_possible_rollback(ptr);
    }
};


// T should be 64 bits or less (we pad it to 64 bits but this could be changed)
template<typename T>
struct tx_field {
    static_assert(sizeof(T) <= sizeof(uint64_t));
    static_assert(std::is_trivially_copyable_v<T>);

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

    bool bloomFilterContainsAddr(uint64_t tableIndex, LockTableNode* ltn, void* addr) const {
        #ifdef COLOCATE_BLOOM_FILTER
        return (ltn->bloom.contains((uint64_t*)addr));
        #else
        return globals.blooms[tableIndex].contains((uint64_t*)addr);
        #endif
    }

    // returns true and stores the data in _val if we versioned this address
    // returns false if we didnt version the address and need to retry
    // aborts the txn if we version the address but our initial read was not consistent
    bool tryVersionAddr(T* _val, OpData* myd, void* addr, uint64_t tableIndex, std::atomic<uint64_t>* mutex, LockTableNode* ltn) const {
        //acquire lock with special versioning tid to flag as locked for versioning
        uint64_t sl = mutex->load(std::memory_order_acquire);
        while (!versioning_tryWriteLock(VERSIONING_TID, mutex, sl)) {
            sl = mutex->load(std::memory_order_acquire);
        }
        assert(isLocked(sl) && getTid(sl) == VERSIONING_TID);

        bool wasReadConsistent = true;
        if (getTS(sl) >= myd->rClock) {
            wasReadConsistent = false;
        }

        //look for this addr in the vlist table
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        if (found) {
            //unlock to the same clock that we acquired on
            uint64_t newSL = (myd->tid << TID_SHIFT) | (getTS(sl));
            mutex->store(newSL, std::memory_order_release);
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

        VersionedNode* vNode = (VersionedNode*)(malloc(sizeof(VersionedNode)));
        // write current version (no need to mark TBD since we havent linked this vnode into the list yet)
        vNode->ts.store(lastTs, std::memory_order_relaxed);
        // write current value - the order of this write relative to the TS write is also not relevant since the vnode is not yet reachable
        uint64_t data_bit_copied_to_u64 = 0;
        std::memcpy(&data_bit_copied_to_u64, &val, sizeof(T));            
        vNode->data = data_bit_copied_to_u64;
        vNode->next = nullptr;

        vtn = (VListTableNode*)(malloc(sizeof(VListTableNode)));
        vtn->addr = addr;
        vtn->listHead.store(vNode, std::memory_order_relaxed);
        vtn->next = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
        globals.gVlistTable[tableIndex].store(vtn, std::memory_order_relaxed);

        #ifdef COLOCATE_BLOOM_FILTER
        if (!ltn->bloom.contains((uint64_t*)addr)) {
            ltn->bloom.insertFreshUnsafe((uint64_t*)addr);
        }
        #else
        if (!globals.blooms[tableIndex].contains((uint64_t*)addr)) {
            globals.blooms[tableIndex].insertFreshUnsafe((uint64_t*)addr);
        }
        #endif

        //unlock to the same clock that we acquired on
        //  note that the lock MUST have been unlocked since snapshots are only
        //  enabled for read-only txns in this version
        uint64_t newSL = (myd->tid << TID_SHIFT) | (getTS(sl));
        assert(isUnlocked(newSL));
        mutex->store(newSL, std::memory_order_release);

        if (wasReadConsistent) {
            *_val = val;
            return true;
        }
        else {
            abortTx(myd);
        }
    }

    FORCE_NO_INLINE T snapshotLoad() const {
        OpData* const myd = tl_opdata;
        void* addr = (void*)(&data);
        uint64_t tableIndex = hidx((size_t)addr);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];

        if (getCounterMode(myd->myTmModeCounter) == TM_MODE_MODE2) {
            return mode2_snapshotLoad(myd, addr, tableIndex, ltn);
        }
        else {
            return defaultMode_snapshotLoad(myd, addr, tableIndex, ltn);
        }
    }

    VersionedNode* traverseVList(OpData* myd, VListTableNode* vtn) const {
        VersionedNode* vNode = vtn->listHead.load(std::memory_order_relaxed);
        uint64_t markedTs = vNode->ts.load(std::memory_order_acquire);

        while (isTBD(markedTs) && (getUnmarkedTimestamp(markedTs) <= myd->rClock)) {
            markedTs = vNode->ts.load(std::memory_order_acquire);
        }

        bool traversedPastHead = false;
        while (vNode && (getUnmarkedTimestamp(markedTs) > myd->rClock) || markedTs == EMPTY_SNAPSHOT_TIMESTAMP) {
            vNode = vNode->next;
            if (!vNode) {
                break;
            }
            markedTs = vNode->ts.load(std::memory_order_acquire);
            traversedPastHead = true;
        }

        if (traversedPastHead) {
            myd->numSnapReadsPastHead++;
        }

        return vNode;
    }

    void updateVlist(OpData* myd, VListTableNode* vtn, VersionedNode* head, uint64_t newVal) {
        uint64_t ts = head->ts.load(std::memory_order_relaxed);
        uint64_t data_bit_copied_to_u64 = 0;
        std::memcpy(&data_bit_copied_to_u64, &newVal, sizeof(T));
        if (isTBD(ts)) {
            //this vnode is marked TBD but we hold the lock so we must have been the one to add the TBD
            head->data = data_bit_copied_to_u64;
        }
        else {
            //this is the first time we are writting to this address
            VersionedNode* vNode = (VersionedNode*)malloc(sizeof(VersionedNode));
            vNode->next = head;
            vNode->ts.store(makeTBDSnapshotTimestamp(myd->rClock), std::memory_order_relaxed);
            vNode->data = data_bit_copied_to_u64;
            vtn->listHead.store(vNode, std::memory_order_relaxed);
            myd->snapshotWriteSet.add(vtn);
            gMemManager.deferredFree_with_possible_rollback(head);
        }
    }

    T defaultMode_snapshotLoad(OpData* myd, void* addr, uint64_t tableIndex, LockTableNode* ltn) const {
        uint64_t retryCount = 0;
        while (retryCount < MAX_ABORTS) {
            retryCount++;
            VListTableNode* vtn = nullptr;
            bool found = false;
            T val;

            if (bloomFilterContainsAddr(tableIndex, ltn, addr)) {
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

            VersionedNode* vNode = traverseVList(myd, vtn);

            if (!vNode) {
                abortTx(myd);
            }

            val = 0;
            std::memcpy(&val, &vNode->data, sizeof(T)); // (T)(vNode->data);

            return val;
        }
        printf("Snapshot load stuck retrying. Existing\n");
        exit(-3);
    }

    T mode2_snapshotLoad(OpData* myd, void* addr, uint64_t tableIndex, LockTableNode* ltn) const {
        uint64_t retryCount = 0;
        uint64_t lastSL = 0;
        T lastVal = (T)0;
        bool found = false;
        while (retryCount < MAX_SNAPSHOT_MODE2_RETRIES_BEFORE_LOCKING*10) {
            retryCount++;
            VListTableNode* vtn = globals.gVlistTable[tableIndex].load(std::memory_order_relaxed);
            T val;

            if (!found && bloomFilterContainsAddr(tableIndex, ltn, addr)) {
                findAddrInVLT(myd, addr, tableIndex, vtn, found);
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
                        return lastVal;
                    }
                    lastSL = sl;
                    lastVal = data;
                    continue;
                }
                val = data;
                uint64_t sl2 = mutex->load(std::memory_order_acquire);
                if (sl2 != sl) {
                    abortTx(myd);
                }
                return val;
            }

            VersionedNode* vNode = traverseVList(myd, vtn);

            if (!vNode) {
                abortTx(myd);
            }

            val = 0;
            std::memcpy(&val, &vNode->data, sizeof(T)); // (T)(vNode->data);
            return val;
        }
        assert(false);
        exit(-1);
    }

    FORCE_INLINE bool tryVersionedStore(OpData* myd, void* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn) {
        //check if the addr has a vlist - if it does not then we hit a false positive in the bloom filter
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        if (!found) {
            return false;
        }

        VersionedNode* head = vtn->listHead.load(std::memory_order_relaxed);

        assert(head != nullptr);

        uint64_t oldValue = 0;
        std::memcpy(&oldValue, addr, sizeof(T));
        myd->writeSet.add(addr, oldValue, sizeof(T));
        data = newVal;

        updateVlist(myd, vtn, head, (uint64_t)newVal);

        return true;
    }

    #ifdef ENABLE_SNAPSHOT_ISOLATION
    void snapshotIsolationStore(OpData* myd, void* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn) {
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
            uint64_t oldValue = 0;
            std::memcpy(&oldValue, addr, sizeof(T));
            myd->writeSet.add(addr, oldValue, sizeof(T));
            data = newVal;
            return;
        }

        VersionedNode* head = vtn->listHead.load(std::memory_order_relaxed);

        assert(head != nullptr);

        uint64_t oldValue = 0;
        std::memcpy(&oldValue, addr, sizeof(T));
        myd->writeSet.add(addr, oldValue, sizeof(T));
        data = newVal;

        updateVlist(myd, vtn, head, (uint64_t)newVal);
    }
    #endif

    void forceVersionedStore(OpData* myd, void* addr, T newVal, uint64_t tableIndex, LockTableNode* ltn, uint64_t lastLockTs) {
        VListTableNode* vtn;
        bool found;
        findAddrInVLT(myd, addr, tableIndex, vtn, found);

        VersionedNode* head;
        if (!found) {
            VersionedNode* vNode = (VersionedNode*)(malloc(sizeof(VersionedNode)));
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
            uint64_t data_bit_copied_to_u64 = 0;
            std::memcpy(&data_bit_copied_to_u64, addr, sizeof(T));            
            vNode->data = data_bit_copied_to_u64;
            vNode->next = nullptr;

            vtn = (VListTableNode*)(malloc(sizeof(VListTableNode)));
            vtn->addr = addr;
            vtn->listHead.store(vNode, std::memory_order_relaxed);

            vtn->next = globals.gVlistTable[tableIndex].load(std::memory_order_acquire);
            globals.gVlistTable[tableIndex].store(vtn, std::memory_order_release);

            head = vNode;
        }
        else {
            head = vtn->listHead.load(std::memory_order_relaxed);
        }

        #ifdef COLOCATE_BLOOM_FILTER
        if (!ltn->bloom.contains((uint64_t*)addr)) {
            ltn->bloom.insertFreshUnsafe((uint64_t*)addr);
        }
        #else
        if (!globals.blooms[tableIndex].contains((uint64_t*)addr)) {
            globals.blooms[tableIndex].insertFreshUnsafe((uint64_t*)addr);
        }
        #endif

        assert(vtn != nullptr);
        assert(head != nullptr);

        uint64_t oldValue = 0;
        std::memcpy(&oldValue, addr, sizeof(T));
        myd->writeSet.add(addr, oldValue, sizeof(T));        
        data = newVal;

        updateVlist(myd, vtn, head, (uint64_t)newVal);

        return;
    }

    FORCE_INLINE void store(T newVal) {
        OpData* const myd = tl_opdata;

        #ifdef ALLOW_UNSAFE_STORES_IN_CONSTRUCTOR
        if (myd->constructorCount != 0) {
            data = newVal;
            return;
        }
        #endif

        void* addr = (void*)&data;
        uint64_t tableIndex = hidx((size_t)addr);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];
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
                abortTx(myd);
            }
            break;
        }

        if (!tryWriteLock(myd->tid, mutex, sl)) {
            abortTx(myd);
        }

        if (myd->forceVersioning) {
            forceVersionedStore(myd, addr, newVal, tableIndex, ltn, getTS(sl));
            return;
        }

        if (__glibc_unlikely(bloomFilterContainsAddr(tableIndex, ltn, addr))) {
            if (tryVersionedStore(myd, addr, newVal, tableIndex, ltn)) {
                data = newVal;
                return;
            }
        }

        uint64_t oldValue = 0;
        std::memcpy(&oldValue, addr, sizeof(T));
        myd->writeSet.add(addr, oldValue, sizeof(T));
        data = newVal;
    }

    FORCE_INLINE T load_tl2() const {
        OpData* const myd = tl_opdata;
        void* addr = (void*)(&data);
        uint64_t tableIndex = hidx((size_t)addr);
        LockTableNode* ltn = &globals.gHashLock[tableIndex];
        std::atomic<uint64_t>* mutex = &ltn->mutex;

        T val = data;
        while (true) {
            uint64_t sl = mutex->load(std::memory_order_acquire);
            if (!isReadConsistent(myd->rClock, myd->tid, sl)) {
                if (isInprogressVersioning(sl)) {
                    continue;
                }
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
        // myd->readCount++;

        if (__glibc_unlikely(myd->tx_type & TX_IS_SNAPSHOT)) {
            return snapshotLoad();
        }
        return load_tl2();
    }
};


//
// Wrapper methods to the global TM instance. The user should use these:
//
template<typename R, typename F> static R updateTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_UPDATE); }
template<typename R, typename F> static R readTx(F&& func) { return gMultiverse.transaction<R>(func, TX_IS_READ); }
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
    myd->writeSet.rollback(myd->tid);
    myd->snapshotWriteSet.rollback(myd->tid);

    std::atomic_thread_fence(std::memory_order_seq_cst);

    #ifndef USE_RDTSC
    // uint64_t nextClock = globals.gClock.fetch_add(1);

    uint64_t nextClock;
    uint64_t curr = globals.gClock.load();
    if (curr != myd->rClock) {
        nextClock = curr;
    } else if (globals.gClock.compare_exchange_strong(curr, curr + 1)) {
        nextClock = curr + 1;
    } else {
        // curr side-effected to contain actual value of gClock
        nextClock = curr;
    }
    #else
    uint64_t nextClock = rdtsc_get_abort_ts();
    #endif

    // Unlock with the next sequence
    myd->writeSet.unlock(nextClock, myd->tid);
    gMemManager.abort();

    if (myd->tx_type == TX_IS_READ) {
        uint64_t maxRdSetSize = MAX_SNAPSHOT_DEFAULT_MODE_RD_SET_SIZE.load(std::memory_order_relaxed);
        if (maxRdSetSize != 0 && myd->attempt >= RETRIES_BEFORE_CHECKING_READ_SET_SIZE_FOR_MODE2 && myd->readCount >= maxRdSetSize) {
            #ifndef DISABLE_MODE2_SNAPSHOTS
            transitionToMode2(myd);
            #endif
        }
        else if(myd->attempt > RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE && !myd->forceMode2Reads) {
            myd->tx_type = TX_IS_SNAPSHOT;
        }
        else if(myd->attempt > RETRIES_BEFORE_SNAPSHOT_DEFAULT_MODE && myd->forceMode2Reads) {
            #ifndef DISABLE_MODE2_SNAPSHOTS
            transitionToMode2(myd);
            #endif
        }
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