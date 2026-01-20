// #pragma once
#ifndef TM_MEMORY_MANAGER
#define TM_MEMORY_MANAGER

#include <atomic>
#include <chrono>
#include <cassert>

#ifndef PAD
#define CAT2(x, y) x##y
#define CAT(x, y) CAT2(x, y)
#define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#ifndef MAX_THREADS_POW2
#define MAX_THREADS_POW2 256
#endif

#include "tm_alloc2.h"
#include <numa.h>

struct padded_atomic_long {
    std::atomic<long> data;
    PAD;
};

template <int BUFF_SIZE> 
struct VoidPtrBuffer {
    void* list[BUFF_SIZE];
    uint64_t size;
    VoidPtrBuffer* next;

    void reset() {
        size = 0;
        if (next) {
            clearOverflowBuffer(next);
        }
    }

    void clearOverflowBuffer(VoidPtrBuffer* buff) {
        if (!buff) {
            return;
        }

        if (buff->next) {
            clearOverflowBuffer(buff->next);
            free(buff);
        }
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

    void onAbort() {             
        while (activeTxnLimboBag) {
            LimboBagNode* tmp = activeTxnLimboBag;
            activeTxnLimboBag = activeTxnLimboBag->next;
            delete tmp;
        }
    }

    void reset() {
    }
};

struct AllocInfo {
    uint64_t tid;
    EBR_State myEbrState;
    AllocBufferNode* mallocList;
    uint64_t retireClears = 0;
    TMAlloc* myAlloc;
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

alignas(128) thread_local AllocInfo* tl_allocInfo;

class MemoryManager {
    TMAlloc tmAlloc_numa0;
    TMAlloc tmAlloc_numa1;
    void* base_numa0;
    size_t mapSize_numa0;
    void* base_numa1;
    size_t mapSize_numa1;

public:
    AllocInfo perThreadAllocInfo[MAX_THREADS_POW2];
    std::atomic<long> gEpoch {0};
    PAD;
    padded_atomic_long gThreadEpochs[MAX_THREADS_POW2];
    PAD;

    MemoryManager() {
        for (int i = 0; i < MAX_THREADS_POW2; i++) {            
            perThreadAllocInfo[i].tid = i;
            perThreadAllocInfo[i].myEbrState.sysTime = std::chrono::system_clock::now();
            perThreadAllocInfo[i].myEbrState.currEpoch = 0;
            perThreadAllocInfo[i].myEbrState.ClearBagRetries = 0;
            gThreadEpochs[i].data.store(-1, std::memory_order_relaxed);
        }
    }

    inline void init(void* baseAddr_numa0, size_t mapSize_numa0, void* baseAddr_numa1, size_t mapSize_numa1) {
        tmAlloc_numa0.init(baseAddr_numa0, mapSize_numa0);
        tmAlloc_numa1.init(baseAddr_numa1, mapSize_numa1);
    }  
    
    inline void initThread(int tid){
        int cpu = sched_getcpu();
        int node = numa_node_of_cpu(cpu);
        if (node > 1) {
            printf("Tid=%d\n", tid);
            printf("cpu=%d\n", cpu);
            printf("Error: memory manager is setup for max 2 numa nodes.\n");
            printf("To use more than 2 nodes expand the number of allocator instances and init them appropriately.\n");
            exit(-1);
        }
        if (node == 0) {
            perThreadAllocInfo[tid].myAlloc = &tmAlloc_numa0;
        }   
        else {
            perThreadAllocInfo[tid].myAlloc = &tmAlloc_numa1;
        }     
    }

    inline void freePrevEpochLimboBag(AllocInfo* myAllocInfo) {
        EBR_State* ebr = &myAllocInfo->myEbrState;
        freeBuffContents(ebr->prevEpochLimboBag); 
        ebr->prevEpochLimboBag = ebr->currEpochLimboBag;
        ebr->currEpochLimboBag = nullptr;
        ebr->currEpoch = gEpoch.load();
    }

    inline void commit(AllocInfo* allocInfo) {
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
            milliseconds_between_epoch_updates * (1 + ((float) allocInfo->tid)/MAX_THREADS_POW2)) {
            ebr->ClearBagRetries = 0;
            ebr->sysTime = timeNow;
            tryAdvanceGlobalEpoch();
        }
    }

    inline void abort(AllocInfo* allocInfo) {
        freeBuffContents(allocInfo->mallocList);
        allocInfo->myEbrState.onAbort();
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
        for (int i = 0; i < MAX_THREADS_POW2; i++) {
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
            deallocate(buff->ptr);
            buff = buff->next;
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
        //dumb hack for unsafe txns
        int tid = 0;
        if (tl_allocInfo == nullptr) {            
            tl_allocInfo = &perThreadAllocInfo[0];
        }
        tid = tl_allocInfo->tid;
        // ptr = tmAlloc.doMalloc(tid, size);
        ptr = tl_allocInfo->myAlloc->doMalloc(tid, size);
        assert(ptr != nullptr);
        return ptr;
    }

    inline void* bufferedAllocate(size_t size) {
        void* ptr = nullptr;
        ptr = tl_allocInfo->myAlloc->doMalloc(tl_allocInfo->tid, size);        
        tl_allocInfo->mallocList = addItemToBuffer(tl_allocInfo->mallocList, ptr);
        assert(ptr != nullptr);
        return ptr;
    }

    inline void deferFree(void* ptr) {
        assert(ptr != nullptr);
        retireWithPossibleRollback(ptr);
    }

    inline void deallocate(void* ptr) {
        assert(ptr != nullptr);
        //dumb hack for unsafe txns
        int tid = 0;
        if (tl_allocInfo) {
            tid = tl_allocInfo->tid;
        }
        if (ptr < base_numa1) {
            tmAlloc_numa0.doFree(tid, ptr);    
        }
        else {
            tmAlloc_numa1.doFree(tid, ptr);    
        }
        // tmAlloc.doFree(tid, ptr);
    }
};

//singleton
MemoryManager gMemManager;
#endif