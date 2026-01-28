/*
 * This is a prototype of DCOTL based on the TrinityVRTL2 implementation. The original
 *   authors of Trinity are Pedro Ramalhete: pramalhe@gmail.com, Andreia Correia: andreia.veiga@unine.ch
 *   and Pascal Felber: pascal.felber@unine.ch
 *
 *   Most of this file is from https://github.com/pramalhe/durabletx/blob/master/ptms/trinity/TrinityVRTL2.hpp
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

#include "tm_common/stats.h"
#include "tm_common/tm_timelines.h"

#define TM_IS_DCTL 1
int DCTL_K_FAILS = 10000;
static const int MAX_K_FAILS = 100;

namespace ns_dcotl {

#define FORCE_INLINE __attribute__((always_inline))
#define FORCE_INLINE2 __attribute__((always_inline))
// #define FORCE_INLINE __attribute__((noinline))
// #define FORCE_INLINE2 __attribute__((always_inline))

// #define USE_TM_ALLOC 1
// #define DEBUG_TRACK_TXN_DURATION 1

#if defined(USE_TM_ALLOC)
    #include "tm_alloc2.h"
#endif

#define MAX_TM_TYPE_BYTES 8

#define TRACK_REGULAR_METRICS 1

//GUY::TODO:: we dont really need to use their tm allocator but for now I left it in
//  this requires knowing the max memory size at compile time which is restrictive
//  consider adding the ability to have multiple mapped regions so we dont cap memory at compile time
#ifndef VOLATILE_MEMORY_REGION_SIZE
    #define VOLATILE_MEMORY_REGION_SIZE 200*1024*1024*1024ULL //GB
#endif

static const int MAX_ABORTS = 10000;

// Maximum number of registered threads that can execute transactions
static const int REGISTRY_MAX_THREADS = 256;

// TL2 constants
static const uint64_t LOCKED    = 0x8000000000000000ULL;
static const int TX_IS_NONE     = 0;
static const int TX_IS_READ     = 1;
static const int TX_IS_UPDATE   = 2;
static const int TX_IS_IRREVOCABLE   = 3;
// Number of striped locks. _Must_ be a power of 2.
static const uint64_t NUM_LOCKS = 64*1024*1024;


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

inline FORCE_INLINE2 static uint64_t hidx(size_t addr) {    
    // return (((addr + 128) >> 2) & (NUM_LOCKS-1));
    return ((addr >> 6) & (NUM_LOCKS-1));  // Thanks to Pedro Ramalhete for suggesting this improved mapping function
}

inline static void Pause(uint64_t numIters) {
    //----------
    for (int i = 0; i < numIters; i++) {
        SOFTWARE_BARRIER;
    }
}

class TicketLock {
public:
    std::atomic<uint64_t> nextTicket {0};
    std::atomic<uint64_t> nowServing {0};
    static const int base = 1;

    void acquire() {
        uint64_t ticket = nextTicket.fetch_add(1);
        while (true) {
            uint64_t currServing = nowServing.load();
            if (ticket == currServing) {
                // printf("acquired ticket\n");
                break;
            }
            Pause(base * (ticket - currServing));
        }
        std::atomic_thread_fence(std::memory_order_seq_cst);
    }

    void release() {
        // printf("Released ticket\n");
        uint64_t ticket = nowServing.load(std::memory_order_relaxed) + 1;
        nowServing.store(ticket, std::memory_order_release);
    }
};


class RWSeqLock {
public:
    std::atomic<uint64_t> state {0};

    static const uint64_t UNLOCKED = 0;    // 00 | TID | TS
    static const uint64_t RLOCKED = 1;     // X1 | TID | TS
    static const uint64_t WLOCKED = 2;     // 1X | TID | TS

    static const uint64_t RLOCKED_DELTA = 0x4000000000000000ULL;

    static const int TID_BITSHIT = 52;
    static const int LOCKSTATE_BITSHIFT = 62;
    static const uint64_t LOCKSTATE_BITMASK = 0xc000000000000000ULL;    // LOCK (2) | TID (10) | TS (52), LOCK = 3ULL << LOCKSTATE_BITSHIFT
    static const uint64_t TID_BITMASK = 0x3ff0000000000000ULL;          // LOCK (2) | TID (10) | TS (52), TID = 1023ULL << TID_BITSHIT
    static const uint64_t TS_BITMASK = 0xfffffffffffffULL;              // LOCK (2) | TID (10) | TS (52)

    inline static uint64_t getLockState(uint64_t sl) {
        return (sl & LOCKSTATE_BITMASK) >> LOCKSTATE_BITSHIFT;
    }

    inline static bool isWLocked(uint64_t sl) {
        return (getLockState(sl) & WLOCKED);
    }

    inline static bool isRLocked(uint64_t sl) {
        return (getLockState(sl) & RLOCKED);
    }

    inline static bool isUnlocked(uint64_t sl) {
        return (getLockState(sl) == UNLOCKED);
    }

    inline static uint64_t getTid(uint64_t sl) {
        return ((sl & TID_BITMASK) >> TID_BITSHIT);
    }

    inline static uint64_t getTS(uint64_t sl) {
        return (sl & TS_BITMASK);
    }

    inline static uint64_t compose(uint64_t lockState, uint64_t tid, uint64_t ts) {
        uint64_t newSL = (lockState << LOCKSTATE_BITSHIFT) | (tid << TID_BITSHIT) | (ts);
        return newSL;
    }

    inline static bool isReadConsistent(uint64_t rClock, uint64_t tid, uint64_t sl) {
        return ((isUnlocked(sl) && getTS(sl) < rClock) || (getTid(sl) == tid));
    }

    // inline static bool isWriteConsistent(uint64_t tid, uint64_t sl) {
    inline static bool isWriteConsistent(uint64_t rClock, uint64_t tid, uint64_t sl) {
        // return (isUnlocked(sl) || (isLocked(sl) && getTid(sl) == tid));
        return isReadConsistent(rClock, tid, sl);
    }

    bool tryWriteLock(const int tid) { // called from speculative write transactions
        uint64_t sl = state.load();
        if (isWLocked(sl)) return (getTid(sl) == tid);
        if (isRLocked(sl)) return false;
        return (state.compare_exchange_strong(sl, compose(WLOCKED,tid,getTS(sl))));
    }

    bool tryWriteLock(const int tid, uint64_t sl) { // called from speculative write transactions
        if (isWLocked(sl)) return (getTid(sl) == tid);
        if (isRLocked(sl)) return false;
        return (state.compare_exchange_strong(sl, compose(WLOCKED,tid,getTS(sl))));
    }

    void writeLock(const int tid) { // called from irrevocable write transactions
        uint64_t sl = state.load();
        if (isWLocked(sl) && getTid(sl) == tid) return;
        if (!isRLocked(sl)) {
            state.fetch_add(RLOCKED_DELTA);
        }
        while (isWLocked(state.load())) Pause(1); // spin loop
        state.store(compose(WLOCKED,tid,getTS(sl)), std::memory_order_release);
    }

    void readLock(const int tid) { // called from irrevocable write transactions
        uint64_t sl = state.load();
        if (isRLocked(sl)) return;
        if (isWLocked(sl) && getTid(sl) == tid) return;
        state.fetch_add(RLOCKED_DELTA);
        while (isWLocked(state.load())) Pause(1); // spin loop
    }

    void unlock(const int tid, const int txType, uint64_t ts=0) { // unlock read/write−locks
        uint64_t sl = state.load();
        if (getTid(sl) != tid) return;
        if (isRLocked(sl) && txType == TX_IS_IRREVOCABLE) {
            state.fetch_sub(RLOCKED_DELTA);
        }
        if (isWLocked(sl) && getTid(sl) == tid) {
            uint64_t delta = sl - compose(UNLOCKED, tid, ts);
            state.fetch_sub(delta);
        }
    }
};



struct padded_atomic_long {
    std::atomic<long> data;
    PAD;
};

struct padded_atomic_u64 {
    std::atomic<uint64_t> data;
    PAD;
};


struct Globals {
    PAD;
    // std::atomic<uint64_t> gHashLock[NUM_LOCKS];
    RWSeqLock gHashLock[NUM_LOCKS];
    PAD;
    std::atomic<uint64_t> gClock {1};
    PAD;
    TicketLock gTicketLock;
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

// Forward declaration of global/singleton instance
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
        // gMemManager.unanounceEpoch(tid);
        gMemManager.forceClearBags();
        gMemManager.unanounceEpoch(tid);
        usedTID[tid].store(false, std::memory_order_release);
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


// struct tmbase {
//     ~tmbase() {
//         printf("tmbase destructor\n");
//     }
// };


struct ReadSet {
    struct ReadSetEntry {
        RWSeqLock* lock;
    };

    static const int64_t MAX_ENTRIES = 16*1024;

    uint64_t      size {0};          // Number of loads in the readSet for the current transaction
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
            std::atomic<uint64_t>* mutex = &entries[i].lock->state;
            uint64_t sl = mutex->load(std::memory_order_acquire);
            if (!RWSeqLock::isReadConsistent(rClock, tid, sl)) {
                return false;
            }
        }
        if (next != nullptr) return next->validate(rClock, tid); // Recursive call to validate()
        return true;
    }

    inline void add(RWSeqLock* lock) {
        if (size != MAX_ENTRIES) {
            entries[size].lock = lock;
            size++;
        } else {
            if (next == nullptr) {
                next = new ReadSet();
            }
            next->add(lock);
        }
    }

    inline void unlock(const int tid, const int txType) {
        for (uint64_t i = 0; i < size; i++) {
            entries[i].lock->unlock(tid, txType);
        }
        if (next != nullptr) {
            next->unlock(tid, txType);
        }
    }
};


struct AppendLog {
    static const int64_t MAX_ENTRIES = 8*1024;

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

    // Rollback modifications
    void rollback(uint64_t tid) {
        if (size == 0) return;
        if (next != nullptr) {
            next->rollback(tid);
        }
        for (int64_t i = size-1; i >= 0; i--) {
            *(entries[i].addr) = entries[i].oldValue;
        }
    }

    // Unlock all the locks acquired by this thread
    inline void unlock(uint64_t nextClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            uint64_t* addr = entries[i].addr;
            std::atomic<uint64_t>* mutex = &globals.gHashLock[hidx((size_t)addr)].state;
            uint64_t sl = mutex->load(std::memory_order_relaxed);
            if (!RWSeqLock::isUnlocked(sl) && RWSeqLock::getTid(sl) == tid) {
                uint64_t newSL = (tid << RWSeqLock::TID_BITSHIT) | (nextClock);
                mutex->store(newSL, std::memory_order_release);
            }
        }
        if (next != nullptr) {
            next->unlock(nextClock, tid);
        }
    }
};


// Thread-local data
struct OpData {
    alignas(64) uint64_t        rClock {0};
    uint64_t        tid;    
    int        tx_type {TX_IS_NONE};        
    ReadSet         readSet;
    AppendLog       writeSet;
    uint64_t        myrand;
    std::jmp_buf    env;
    uint64_t        attempt {0};
    #ifndef DEBUG_TRACK_TXN_DURATION
    uint64_t     padding[18];
    #else
    uint64_t     num_regular_txn_commit;
    uint64_t     num_regular_txn_abort;
    uint64_t     txn_start_time;
    uint64_t     txn_begin_time;
    double     total_regular_txn_ms_time_at_commit;
    double     total_regular_txn_attempt_ms_time_at_abort;
    uint64_t     padding[12];
    #endif
};

[[noreturn]] extern void abortTx(OpData* myd);

alignas(128) extern thread_local OpData* tl_opdata;


class DCOTL;
extern DCOTL gDCOTL;


class DCOTL {
private:
    alignas(128) OpData                   *opDesc;
    alignas(128) std::atomic<int>         debugLock {0};
public:
    // struct tmbase : public ns_dcotl::tmbase { };

    DCOTL() {
        printf("sizeof(OpData)=%ld\n", sizeof(OpData));
        printf("sizeof(AppendLog)=%ld\n\n", sizeof(AppendLog));

        opDesc = new OpData[REGISTRY_MAX_THREADS];
        for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
            opDesc[it].tid = it;
            opDesc[it].myrand = (it+1)*12345678901234567ULL;
        }
        printf("\n");

        mapVolatileRegion();
    }

    ~DCOTL() {
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            opDesc[it].writeSet.reset();
            opDesc[it].readSet.reset();
        }

        #ifdef DEBUG_TRACK_TXN_DURATION
        double all_thread_total_time_at_commit = 0;
        uint64_t all_thread_total_commits = 0;
        printf("Average_ms_per_commit_per_thread:\n");
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            if (opDesc[it].num_regular_txn_commit != 0) {
                double avg_time_per_commit = (opDesc[it].total_regular_txn_ms_time_at_commit / opDesc[it].num_regular_txn_commit);
                all_thread_total_commits += opDesc[it].num_regular_txn_commit;
                all_thread_total_time_at_commit += opDesc[it].total_regular_txn_ms_time_at_commit;
                printf("%f ", avg_time_per_commit);
            }
        }
        printf("overall_avg_ms_per_commit=%f\n", all_thread_total_time_at_commit / all_thread_total_commits);
        printf("\n");

        double all_thread_total_time_at_abort = 0;
        uint64_t all_thread_total_aborts = 0;
        printf("Average_ms_per_abort_per_thread:\n");
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            if (opDesc[it].num_regular_txn_abort != 0) {
                double avg_time_per_abort = (opDesc[it].total_regular_txn_attempt_ms_time_at_abort / opDesc[it].num_regular_txn_abort);
                all_thread_total_time_at_abort += opDesc[it].total_regular_txn_attempt_ms_time_at_abort;
                all_thread_total_aborts += opDesc[it].num_regular_txn_abort;
                printf("%f ", avg_time_per_abort);
            }
        }
        printf("overall_avg_ms_per_abort=%f\n", all_thread_total_time_at_abort / all_thread_total_aborts);
        printf("\n");
        #endif

        int numUsedLocks = 0;
        for (int i = 0; i < NUM_LOCKS; i++) {
            if (globals.gHashLock[i].state.load()) {
                numUsedLocks++;
            }
        }
        printf("num_occupied_locks=%d\n", numUsedLocks);
        printf("percentage_of_occupied_locks=%f\n", ((numUsedLocks * 1.0f) / NUM_LOCKS) * 100.0f);


        #ifdef NO_OPTIMIZE_IS_ON
        printf("\n");
        printf("\n");
        printf("WARNING: THIS BINARY WAS COMPILED WITH NO OPTMIZATIONS. This is not appropriate for experiment results.\n");
        printf("NO_OPTMIZATIONS_IS_ON=1\n");
        printf("\n");
        printf("\n");
        #endif

        delete[] opDesc;
    }

    // Maps the volatile memory region
    void mapVolatileRegion() {
        #ifdef USE_TM_ALLOC
        void* mapAddr = mmap(NULL, VOLATILE_MEMORY_REGION_SIZE, (PROT_READ | PROT_WRITE), MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (mapAddr == MAP_FAILED) {
            perror("mmap error:\n ");
            printf("-------------\n");
            exit(-1);
        }
        gMemManager.init(mapAddr, VOLATILE_MEMORY_REGION_SIZE);
        #endif
    }

    inline void beginTx(OpData* myd, const int tid) {
        #ifdef DEBUG_TRACK_TXN_DURATION
        myd->txn_begin_time = get_server_clock();
        // myd->txn_begin_time = 13;
        // uint64_t x = 10;
        // uint64_t y = 11;
        #endif

        gMemManager.announceEpoch(myd->tid);
        // Clear the logs of the previous transaction
        myd->writeSet.reset();
        myd->readSet.reset();
        tl_allocInfo->reset();
        myd->rClock = globals.gClock.load(); // This is GVRead()
        if (myd->tx_type == TX_IS_IRREVOCABLE) globals.gTicketLock.acquire();
    }

    inline bool endTx(OpData* myd, const int tid) {
        // This fence is needed by undo log to prevent re-ordering with the last store
        // and the reading of the gClock.
        std::atomic_thread_fence(std::memory_order_seq_cst);

        if (myd->tx_type == TX_IS_IRREVOCABLE) {
            uint64_t commitTS = globals.gClock.load();
            myd->writeSet.unlock(commitTS, tid);
            myd->readSet.unlock(myd->tid, myd->tx_type);
            globals.gTicketLock.release();

            STATS_LIST_APPEND(myd->tid, aborts_before_commit, myd->attempt);
            STATS_COUNTER_ADD(myd->tid, commits, 1);
            STATS_COUNTER_ADD(myd->tid, commits_snapshot, 1);
            // printf("irrevocable txn done\n");
            gMemManager.commit(tl_allocInfo);

            // TM_TIMELINE_END((int)myd->tid, "IRREVOCABLE_TXN");
            TM_TIMELINE_END_INMEM((int)myd->tid, IRREVOCABLE_TXN);
            myd->attempt = 0;
            return true;
        }

        // Check if this is a read-only transaction and if so, commit immediately
        if (myd->writeSet.size == 0) {
            myd->attempt = 0;
            #ifdef TRACK_REGULAR_METRICS
            STATS_COUNTER_ADD(myd->tid, commits, 1);
            #endif
            gMemManager.commit(tl_allocInfo);

            #ifdef DEBUG_TRACK_TXN_DURATION
            myd->total_regular_txn_ms_time_at_commit += (get_server_clock() - myd->txn_start_time ) / 1000000.0;
            myd->num_regular_txn_commit++;
            // myd->total_regular_txn_ms_time_at_commit += 13;
            // uint64_t x = 10;
            // uint64_t y = 11;
            #endif

            return true;
        }

        // Validate the read-set
        if (!myd->readSet.validate(myd->rClock, tid)) {
            STATS_COUNTER_ADD(myd->tid, aborts_writer_failed_rdset_validation, 1);
            abortTx(myd);
        }

        uint64_t commitTS = globals.gClock.load(); // timestamp allocation
        myd->writeSet.unlock(commitTS, tid);
        #ifdef TRACK_REGULAR_METRICS
        STATS_COUNTER_ADD(myd->tid, commits, 1);
        #endif
        STATS_LIST_APPEND(myd->tid, aborts_before_commit, myd->attempt);
        STATS_LIST_APPEND(myd->tid, wrset_size_at_commit, myd->writeSet.size);
        STATS_LIST_APPEND(myd->tid, rdset_size_at_commit, myd->readSet.size);
        myd->attempt = 0;

        gMemManager.commit(tl_allocInfo);

        #ifdef DEBUG_TRACK_TXN_DURATION
        myd->total_regular_txn_ms_time_at_commit += (get_server_clock() - myd->txn_start_time ) / 1000000.0;
        myd->num_regular_txn_commit++;
        // myd->total_regular_txn_ms_time_at_commit += 13;
        // uint64_t x = 10;
        // uint64_t y = 11;
        #endif

        return true;
    }

    template<typename F> void transaction(F&& func, int txType=TX_IS_UPDATE) {
        if (tl_opdata != nullptr) {
            func();
            return;
        }
        const int tid = ThreadRegistry::getTID();
        OpData* myd = &opDesc[tid];
        tl_opdata = myd;
        tl_allocInfo = &gMemManager.perThreadAllocInfo[tid];

        // myd->txn_start_time = get_server_clock();
        // myd->txn_start_time = 13;
        // uint64_t x = 10;
        // uint64_t y = 11;

        myd->tx_type = txType;
        setjmp(myd->env);
        myd->attempt++;
        backoff(myd, myd->attempt);
        beginTx(myd, tid);
        func();
        endTx(myd, tid);
        tl_opdata = nullptr;
    }

    // It's silly that these have to be static, but we need them for the (SPS) benchmarks due to templatization
    //template<typename R, typename F> static R updateTx(F&& func) { return gDCOTL.transaction<R>(func, TX_IS_UPDATE); }
    //template<typename R, typename F> static R readTx(F&& func) { return gDCOTL.transaction<R>(func, TX_IS_READ); }
    template<typename F> static void updateTx(F&& func) { gDCOTL.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTx(F&& func) { gDCOTL.transaction(func, TX_IS_READ); }
    // There are no sequential durable transactions in TL2 but we "emulate it" with a concurrent+durable tx
    template<typename F> static void updateTxSeq(F&& func) { gDCOTL.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTxSeq(F&& func) { gDCOTL.transaction(func, TX_IS_READ); }


    // Random number generator used by the backoff scheme
    inline uint64_t marsagliaXORV(uint64_t x) {
        if (x == 0) x = 1;
        x ^= x << 6;
        x ^= x >> 21;
        x ^= x << 7;
        return x;
    }

    // Backoff for a random amount of steps in the range [16, 16*attempt]. Inspired by TL2
    inline void backoff(OpData* myopd, uint64_t attempt) {
        if (attempt < 3) return;
        if (attempt == MAX_ABORTS) {
#ifdef USE_GSTATS
            int expected = 0;
            if (debugLock.compare_exchange_strong(expected, 1)) {
                printf("MAX ABORTS REACHED -- PRINTING GSTATS:\n");
                if (myopd->tx_type == TX_IS_UPDATE) {
                    printf("TXN WAS updater\n");
                }
                else {
                    printf("TXN WAS reader\n");
                }
                GSTATS_PRINT;
                std::cout<<std::endl;
                //intentially forcing seg fault here for debugging core dumps
                int x = *((uint64_t*)(41ull));
                printf("%d\n", x);
                exit(-1);
            }
            std::this_thread::yield();
#endif
        }
        myopd->myrand = marsagliaXORV(myopd->myrand);
        uint64_t stall = (myopd->myrand & attempt) + 1;
        stall *= 16;
        std::atomic<uint64_t> iter {0};
        while (iter.load() < stall) iter.fetch_add(1);
        if (stall > 1000) std::this_thread::yield();
    }

    template<typename R,class F> inline static R readTx(F&& func) {
        gDCOTL.transaction([&]() {func();}, TX_IS_READ);
        return R{};
    }
    template<typename R,class F> inline static R updateTx(F&& func) {
        gDCOTL.transaction([&]() {func();}, TX_IS_UPDATE);
        return R{};
    }

    template <typename T, typename... Args>
    static T* tmNew(Args&&... args) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        void* ptr = gMemManager.bufferedAllocate(sizeof(T));
        // If we get nullptr then we've ran out of PM space
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

// T is typically a pointer to a node, but it can be integers or other stuff, as long as it fits in 64 bits
template<typename T>
struct tx_field {
    static_assert(sizeof(T) <= MAX_TM_TYPE_BYTES);

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

    // Store interposing: acquire the lock and write in 'main'
    inline FORCE_INLINE void store(T newVal) {
        OpData* const myd = tl_opdata;
        uint64_t* addr = (uint64_t*)&data;
        RWSeqLock* lock = &globals.gHashLock[hidx((size_t)addr)];

        if (myd->tx_type == TX_IS_IRREVOCABLE) {
            lock->writeLock(myd->tid); // acquire write−lock
            myd->writeSet.add(addr, *addr); // save the previous data
            data = newVal; // write the new data
            return;
        }

        std::atomic<uint64_t>* mutex = &lock->state;
        uint64_t sl = mutex->load(std::memory_order_acquire);
        if (!RWSeqLock::isWriteConsistent(myd->rClock, myd->tid, sl)) {
            STATS_COUNTER_ADD(myd->tid, aborts_tl2_store_write_inconsistent, 1);
            abortTx(myd);
        }
        else if (!lock->tryWriteLock(myd->tid)) {
            STATS_COUNTER_ADD(myd->tid, aborts_tl2_store_failed_lock_acquire, 1);
            abortTx(myd);
        }

        myd->writeSet.add(addr, *addr); // save the previous data
        data = newVal;
    }

    // This is similar to an undo-log load interposing: do a single post-check
    inline FORCE_INLINE T load() const {
        OpData* const myd = tl_opdata;
        const uint64_t* addr = (uint64_t*)(&data);
        RWSeqLock* lock = &globals.gHashLock[hidx((size_t)addr)];

        if (myd->tx_type == TX_IS_IRREVOCABLE) {
            myd->readSet.add(lock); // needed to unlock read−locks
            lock->readLock(myd->tid); // acquire read−lock
            return data; // read data and return it
        }

        const T val = data;

        // loadLoadFence();

        std::atomic<uint64_t>* mutex = &lock->state;
        uint64_t sl = mutex->load(std::memory_order_acquire);
        if (!RWSeqLock::isReadConsistent(myd->rClock, myd->tid, sl)) {
            STATS_COUNTER_ADD(myd->tid, aborts_tl2_load_read_inconsistent, 1);
            abortTx(myd);
        }
        if (myd->tx_type == TX_IS_UPDATE) {
            myd->readSet.add(lock);
        }
        return val;
    }
};





// //
// // Wrapper methods to the global TM instance. The user should use these:
// //
template<typename R, typename F> static R updateTx(F&& func) { return gDCOTL.transaction<R>(func, TX_IS_UPDATE); }
template<typename R, typename F> static R readTx(F&& func) { return gDCOTL.transaction<R>(func, TX_IS_READ); }
template<typename F> static void updateTx(F&& func) { gDCOTL.transaction(func, TX_IS_UPDATE); }
template<typename F> static void readTx(F&& func) { gDCOTL.transaction(func, TX_IS_READ); }
template<typename T, typename... Args> T* tmNew(Args&&... args) { return DCOTL::tmNew<T>(args...); }
template<typename T> void tmDelete(T* obj) { DCOTL::tmDelete<T>(obj); }
static void* tmMalloc(size_t size) { return DCOTL::tmMalloc(size); }
static void tmFree(void* obj) { DCOTL::tmFree(obj); }


#ifdef MICROBENCH
//
// Place these in a .cpp if you include this header from multiple files (compilation units)
//
// Global/singleton to hold all the thread registry functionality
ThreadRegistry gThreadRegistry {};

Globals globals {};

DCOTL gDCOTL {};

MemoryManager gMemManager {};

thread_local AllocInfo* tl_allocInfo {nullptr};


// Thread-local data of the current ongoing transaction
thread_local OpData* tl_opdata {nullptr};
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
    uint64_t nextClock = globals.gClock.fetch_add(1);
    // printf("aborted, nextclock = %ld\n", nextClock);
    STATS_LIST_APPEND(myd->tid, rdset_size_at_abort, myd->readSet.size);
    STATS_LIST_APPEND(myd->tid, wrset_size_at_abort, myd->writeSet.size);

    gMemManager.abort(myd->tid);

    // Unlock with the new sequence
    myd->writeSet.unlock(nextClock, myd->tid);
    #ifdef TRACK_REGULAR_METRICS
    STATS_COUNTER_ADD(myd->tid, aborts, 1);
    #endif

    if (myd->tx_type == TX_IS_READ) {
        STATS_COUNTER_ADD(myd->tid, read_only_aborts, 1);
    }

    // if (myd->tx_type == TX_IS_READ && myd->attempt == DCTL_K_FAILS) {
    if (myd->attempt == DCTL_K_FAILS) {
        myd->tx_type = TX_IS_IRREVOCABLE;
        STATS_COUNTER_ADD(myd->tid, irrevocable_from_aborts, 1);

        TM_TIMELINE_START((int)myd->tid);
    }

    #ifdef DEBUG_TRACK_TXN_DURATION
    myd->total_regular_txn_attempt_ms_time_at_abort += (get_server_clock() - myd->txn_begin_time ) / 1000000.0;
    myd->num_regular_txn_abort++;
    // myd->total_regular_txn_attempt_ms_time_at_abort += 13;
    // uint64_t x = 10;
    // uint64_t y = 11;
    #endif

    std::longjmp(myd->env, 1);
}
#endif // MICROBENCH

}

