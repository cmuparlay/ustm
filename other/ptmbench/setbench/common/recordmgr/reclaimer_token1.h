/**
 * This file implements four different variants of EBR using token passing.
 * These implementations are conceptually part of Daewoo Kim's thesis.
 *
 * token1:
 * Naive variant of EBR using token passing:
 * - when the token is received,
 * - free the oldest limbo bag,
 * - then pass the token
 *
 * token2:
 * Slightly less naive variant of EBR using token passing:
 * - when the token is received,
 * - pass the token,
 * - then free the oldest limbo bag
 *
 * token3:
 * Even less naive variant of EBR using token passing:
 * - when the token is received,
 * - pass the token,
 * - then free the oldest limbo bag,
 *   - and while freeing, check every 100 objects if we need to pass the token again.
 *   - if so, pass it again
 *
 * token4:
 * Final variant of EBR using token passing:
 * - when the token is received,
 * - pass the token,
 * - then move the contents of the oldest limbo bag to a local freelist,
 * - and amortize the actual free() calls over many startOp() calls.
 *   (i.e., free() one object from the local freelist per startOp() call)
 *
 * note: in data structures that average more than one allocation per startOp call
 *       (on average, over all operations in the workload you're running),
 *       you'll need to free more than one object per startOp call.
 *       there are two obvious ways one could handle this...
 *       1. when you move objects to the freelist, if it's already nonempty,
 *          you can "catch up" and free all garbage in the freelist first.
 *          this is a BAD idea for reasons indicated in the paper
 *          "Are your epochs too epoch? Batch free can be harmful"
 *          by Trevor Brown, Daewoo Kim and Ajay Singh
 *       2. you could also adaptively increase or decrease the number of objects
 *          that are freed in each startOp() call, over time,
 *          based on how much garbage is left over in the freelist whenever
 *          you move objects to the freelist.
 *          having written the paper cited above, i (trevor) now think
 *          this is so-so idea... could work well if you precisely converge on
 *          freeing exactly the number of objects per operation (over time)
 *          that are created per operation (over time). so if you allocate
 *          1.15 objects per operation, you want to free 1 object in each
 *          operation, and 1 additional object every (1/0.15) operations.
 *       you can get an idea of how these would be implemented by looking at
 *       reclaimer_debra.h and searching for DEAMORTIZE_ADAPTIVELY
 *       and CATCH-UP respectively...
 *
 * Copyright (C) 2023 Trevor Brown
 */

#pragma once

#if !defined(vtoken1) && !defined(vtoken2) && !defined(vtoken3) && !defined(vtoken4)
#   define vtoken1
// #   pragma message "defaulting to reclaimer_token1"
// #   warning defaulting to reclaimer_token1
#endif

// #pragma message "RECLAIM_TYPE=" STR(RECLAIM_TYPE)

// #if defined vtoken1
// #   pragma message "using reclaimer_token1"
// // #   warning using reclaimer_token1
// #elif defined vtoken2
// #   pragma message "using reclaimer_token2"
// // #   warning using reclaimer_token2
// #elif defined vtoken3
// #   pragma message "using reclaimer_token3"
// // #   warning using reclaimer_token3
// #elif defined vtoken4
// #   pragma message "using reclaimer_token4"
// // #   warning using reclaimer_token4
// #endif

#include <atomic>
#include <cassert>
#include <iostream>
#include <sstream>
#include <limits.h>
#include "blockbag.h"
#include "plaf.h"
#include "allocator_interface.h"
#include "reclaimer_interface.h"

// optional statistics tracking
#include "gstats_definitions_epochs.h"

template <typename T = void, class Pool = pool_interface<T> >
class reclaimer_token1 : public reclaimer_interface<T, Pool> {
protected:

/*Check and free one object on every operation - by Daewoo*/
#if defined vtoken4
#   define DEAMORTIZE_FREE_CALLS
#endif

#ifdef RAPID_RECLAMATION
#else
#endif

    class ThreadData {
    private:
        PAD;
    public:
        volatile int token;
        int tokenCount; // how many times this thread has had the token
        blockbag<T> * curr;
        blockbag<T> * last;
#ifdef DEAMORTIZE_FREE_CALLS
        blockbag<T> * deamortizedFreeables;
        int numFreesPerStartOp;
#endif
    public:
        ThreadData() {}
    };

    ThreadData threadData[MAX_THREADS_POW2];
    PAD;

public:
    template<typename _Tp1>
    struct rebind {
        typedef reclaimer_token1<_Tp1, Pool> other;
    };
    template<typename _Tp1, typename _Tp2>
    struct rebind2 {
        typedef reclaimer_token1<_Tp1, _Tp2> other;
    };

    inline void getSafeBlockbags(const int tid, blockbag<T> ** bags) {
        setbench_error("unsupported operation");
    }

    long long getSizeInNodes() {
        long long sum = 0;
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            if (threadData[tid].curr)
                sum += threadData[tid].curr->computeSize();
            if (threadData[tid].last)
                sum += threadData[tid].last->computeSize();
        }
        return sum;
    }
    std::string getSizeString() {
        std::stringstream ss;
        ss<<getSizeInNodes();
        return ss.str();
    }

    std::string getDetailsString() {
        std::stringstream ss;
        long long sum[2];

        sum[0] = 0;
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            if (threadData[tid].curr)
                sum[0] += threadData[tid].curr->computeSize();
        }
        ss<<sum[0]<<" ";

        sum[1] = 0;
        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
            if (threadData[tid].last)
                sum[1] += threadData[tid].last->computeSize();
        }
        ss<<sum[1]<<" ";

        return ss.str();
    }

    inline static bool quiescenceIsPerRecordType() { return false; }

    inline bool isQuiescent(const int tid) {
        return false;
    }

    inline static bool isProtected(const int tid, T * const obj) {
        return true;
    }
    inline static bool isQProtected(const int tid, T * const obj) {
        return false;
    }
    inline static bool protect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void unprotect(const int tid, T * const obj) {}
    inline static bool qProtect(const int tid, T * const obj, CallbackType notRetiredCallback, CallbackArg callbackArg, bool memoryBarrier = true) {
        return true;
    }
    inline static void qUnprotectAll(const int tid) {}

    inline static bool shouldHelp() { return true; }

private:
    long long getSizeInNodesForThisThread(int tid) {
        return threadData[tid].curr->computeSizeFast()
             + threadData[tid].last->computeSizeFast();
    }
public:

    // rotate the epoch bags and reclaim any objects retired two epochs ago.
    inline void rotateEpochBags(const int tid) {
        blockbag<T> * const freeable = threadData[tid].last;
        auto freeableSize = freeable->computeSize();
#ifdef GSTATS_HANDLE_STATS
        GSTATS_APPEND(tid, limbo_reclamation_event_size, freeableSize);
        GSTATS_ADD(tid, limbo_reclamation_event_count, 1);
        TIMELINE_START(tid);
#endif

        int numLeftover = 0;
#ifdef DEAMORTIZE_FREE_CALLS
        auto freelist = threadData[tid].deamortizedFreeables;
        if (!freelist->isEmpty()) {
            numLeftover += (freelist->isEmpty()
                    ? 0
                    : (freelist->getSizeInBlocks()-1)*BLOCK_SIZE + freelist->getHeadSize());

            // // "CATCH-UP" bulk free
            // this->pool->addMoveFullBlocks(tid, freelist);
        }
        // TIMELINE_BLIP_Llu(tid, "numFreesPerStartOp", threadData[tid].numFreesPerStartOp);
        freelist->appendMoveFullBlocks(freeable);
        GSTATS_SET_IX(tid, garbage_in_epoch, freelist->computeSizeFast() + getSizeInNodesForThisThread(tid), threadData[tid].tokenCount);
#else
        GSTATS_SET_IX(tid, garbage_in_epoch, getSizeInNodesForThisThread(tid), threadData[tid].tokenCount);
        GSTATS_ADD(tid, limbo_object_frees, freeableSize);
        this->pool->addMoveFullBlocks(tid, freeable); // moves any full blocks (may leave a non-full block behind)

#   if defined vtoken3
        /*passing token even though it is still freeing objects - by Daewoo*/
        int regular_tokenCheck = 0;
        T* ptr;
        while (!freeable->isEmpty()) {
            if (regular_tokenCheck > 100) {
                if (threadData[tid].token) {
                    ++threadData[tid].tokenCount;
                    // pass token
                    threadData[tid].token = 0;
                    threadData[(tid+1) % this->NUM_PROCESSES].token = 1;
#                   ifdef GSTATS_HANDLE_STATS
                        // let's say whenever thread 0 receives the token a new epoch has started...
                        if (tid == 0) {
                            // record a timeline blip for the new epoch
                            TIMELINE_BLIP_INMEM_Llu(tid, blip_advanceEpoch, threadData[tid].tokenCount);
                        }
#                   endif
                }
                regular_tokenCheck = 0;
            }
            ptr = freeable->remove();
            this->pool->add(tid, ptr);
            ++regular_tokenCheck;
        }
#   endif

#endif
        SOFTWARE_BARRIER;

#ifdef GSTATS_HANDLE_STATS
        TIMELINE_END_INMEM_Llu(tid, timeline_rotateEpochBags, threadData[tid].tokenCount);
#endif

        // swap curr and last
        threadData[tid].last = threadData[tid].curr;
        threadData[tid].curr = freeable;
    }

    template <typename... Rest>
    class BagRotator {
    public:
        BagRotator() {}
        inline void rotateAllEpochBags(const int tid, void * const * const reclaimers, const int i) {
        }
    };

    template <typename First, typename... Rest>
    class BagRotator<First, Rest...> : public BagRotator<Rest...> {
    public:
        inline void rotateAllEpochBags(const int tid, void * const * const reclaimers, const int i) {
            typedef typename Pool::template rebindAlloc<First>::other classAlloc;
            typedef typename Pool::template rebind2<First, classAlloc>::other classPool;

            ((reclaimer_token1<First, classPool> * const) reclaimers[i])->rotateEpochBags(tid);
            ((BagRotator<Rest...> *) this)->rotateAllEpochBags(tid, reclaimers, 1+i);
        }
    };

    // returns true if the call rotated the epoch bags for thread tid
    template <typename First, typename... Rest>
    inline bool startOp(const int tid, void * const * const reclaimers, const int numReclaimers, const bool readOnly = false) {
        //SOFTWARE_BARRIER; // prevent token passing from happening before we are really quiescent

        bool result = false;
        if (threadData[tid].token) {
// #if defined GSTATS_HANDLE_STATS
//             GSTATS_APPEND(tid, token_received_time_split_ms, GSTATS_TIMER_SPLIT(tid, timersplit_token_received)/1000000);
//             GSTATS_SET_IX(tid, token_received_time_last_ms, GSTATS_TIMER_ELAPSED(tid, timer_bag_rotation_start)/1000000, 0);
// #endif
#if defined vtoken1
            BagRotator<First, Rest...> rotator;
            rotator.rotateAllEpochBags(tid, reclaimers, 0);
#endif
            // __sync_synchronize();

            ++threadData[tid].tokenCount;

            // pass token
            threadData[tid].token = 0;
            threadData[(tid+1) % this->NUM_PROCESSES].token = 1;
            //__sync_synchronize();

#ifdef GSTATS_HANDLE_STATS
            // let's say whenever thread 0 receives the token a new epoch has started...
            if (tid == 0) {
                // record a timeline blip for the new epoch
                TIMELINE_BLIP_INMEM_Llu(tid, blip_advanceEpoch, threadData[tid].tokenCount);
            }
#endif

// #if defined GSTATS_HANDLE_STATS
//             auto startTime = GSTATS_TIMER_ELAPSED(tid, timer_bag_rotation_start)/1000;
//             GSTATS_APPEND(tid, bag_rotation_start_time_us, startTime);
//             GSTATS_APPEND(tid, bag_rotation_reclaim_size, threadData[tid].last->computeSize());
// #endif
            // rotate bags to reclaim everything retired *before* our last increment,
            // unless tokenCount is 1, in which case there is no last increment.
            // TODO: does it matter? last bag should be empty in this case.
            //       maybe it does, because after rotating it might not be...
            //       on the other hand, those objects *are* retired before this increment...
            //       so, it seems like the if-statement isn't needed.
            //if (threadData[tid].tokenCount > 1) {
#if defined vtoken2 || defined vtoken3 || defined vtoken4
                BagRotator<First, Rest...> rotator;
                rotator.rotateAllEpochBags(tid, reclaimers, 0);
#endif
                result = true;
            //}

// #if defined GSTATS_HANDLE_STATS
//             auto endTime = GSTATS_TIMER_ELAPSED(tid, timer_bag_rotation_start)/1000;
//             GSTATS_APPEND(tid, bag_rotation_end_time_us, endTime);
//             GSTATS_APPEND(tid, bag_rotation_duration_split_ms, (endTime - startTime)/1000);
// #endif
        }

#ifdef DEAMORTIZE_FREE_CALLS
    // TODO: make this work for each object type

    if (!threadData[tid].deamortizedFreeables->isEmpty()) {
        this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
        GSTATS_ADD(tid, limbo_object_frees, 1);
    }
    // if (!threadData[tid].deamortizedFreeables->isEmpty()) {
    //     this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
    // }
    // if (!threadData[tid].deamortizedFreeables->isEmpty()) {
    //     this->pool->add(tid, threadData[tid].deamortizedFreeables->remove());
    // }
#endif

        // in common case, this is lightning fast...
        return result;
    }

    inline void endOp(const int tid) {

    }

    // for all schemes except reference counting
    inline void retire(const int tid, T* p) {
        threadData[tid].curr->add(p);
        DEBUG2 this->debug->addRetired(tid, 1);
    }

    void debugPrintStatus(const int tid) {
//        if (tid == 0) {
//            std::cout<<"this->NUM_PROCESSES="<<this->NUM_PROCESSES<<std::endl;
//        }
//        std::cout<<"token_counts_tid"<<tid<<"="<<threadData[tid].tokenCount<<std::endl;
//        std::cout<<"bag_curr_size_tid"<<tid<<"="<<threadData[tid].curr->computeSize()<<std::endl;
//        std::cout<<"bag_last_size_tid"<<tid<<"="<<threadData[tid].last->computeSize()<<std::endl;
// #if defined GSTATS_HANDLE_STATS
//         GSTATS_APPEND(tid, bag_curr_size, threadData[tid].curr->computeSize());
//         GSTATS_APPEND(tid, bag_last_size, threadData[tid].last->computeSize());
//         GSTATS_APPEND(tid, token_counts, threadData[tid].tokenCount);
// #endif
        if (tid == 0) {
            std::cout<<"global_epoch_counter="<<threadData[0].tokenCount<<std::endl;
        }
    }

    void initThread(const int tid) {
        if (threadData[tid].curr == NULL) {
            threadData[tid].curr = new blockbag<T>(tid, this->pool->blockpools[tid]);
        }
        if (threadData[tid].last == NULL) {
            threadData[tid].last = new blockbag<T>(tid, this->pool->blockpools[tid]);
        }
#ifdef DEAMORTIZE_FREE_CALLS
        threadData[tid].deamortizedFreeables = new blockbag<T>(tid, this->pool->blockpools[tid]);
        threadData[tid].numFreesPerStartOp = 1;
#endif
#ifdef GSTATS_HANDLE_STATS
        GSTATS_CLEAR_TIMERS;
#endif
    }

    void deinitThread(const int tid) {
        // WARNING: this moves objects to the pool immediately,
        // which is only safe if this thread is deinitializing specifically
        // because *ALL THREADS* have already finished accessing
        // the data structure and are now quiescent!!
        if (threadData[tid].curr) {
            this->pool->addMoveAll(tid, threadData[tid].curr);
            delete threadData[tid].curr;
            threadData[tid].curr = NULL;
        }
        if (threadData[tid].last) {
            this->pool->addMoveAll(tid, threadData[tid].last);
            delete threadData[tid].last;
            threadData[tid].last = NULL;
        }
#ifdef DEAMORTIZE_FREE_CALLS
        this->pool->addMoveAll(tid, threadData[tid].deamortizedFreeables);
        delete threadData[tid].deamortizedFreeables;
#endif
    }

    reclaimer_token1(const int numProcesses, Pool *_pool, debugInfo * const _debug, RecoveryMgr<void *> * const _recoveryMgr = NULL)
            : reclaimer_interface<T, Pool>(numProcesses, _pool, _debug, _recoveryMgr) {
        VERBOSE std::cout<<"constructor reclaimer_token1 helping="<<this->shouldHelp()<<std::endl;// scanThreshold="<<scanThreshold<<std::endl;
        for (int tid=0;tid<numProcesses;++tid) {
            threadData[tid].token       = (tid == 0 ? 1 : 0); // first thread starts with the token
            threadData[tid].tokenCount  = 0; // thread with token will update this itself
            threadData[tid].curr = NULL;
            threadData[tid].last = NULL;
#ifdef DEAMORTIZE_FREE_CALLS
            threadData[tid].deamortizedFreeables = NULL;
#endif
        }
    }
    ~reclaimer_token1() {
//        VERBOSE DEBUG std::cout<<"destructor reclaimer_token1"<<std::endl;
//
////        std::cout<<"token_counts=";
////        for (int tid=0;tid<this->NUM_PROCESSES;++tid) std::cout<<threadData[tid].tokenCount<<" ";
////        std::cout<<std::endl;
////
////        std::cout<<"bag_curr_size=";
////        for (int tid=0;tid<this->NUM_PROCESSES;++tid) std::cout<<threadData[tid].curr->computeSize()<<" ";
////        std::cout<<std::endl;
////
////        std::cout<<"bag_last_size=";
////        for (int tid=0;tid<this->NUM_PROCESSES;++tid) std::cout<<threadData[tid].last->computeSize()<<" ";
////        std::cout<<std::endl;
//
//        for (int tid=0;tid<this->NUM_PROCESSES;++tid) {
//
//            // move contents of all bags into pool
//
//            if (threadData[tid].curr) {
//                this->pool->addMoveAll(tid, threadData[tid].curr);
//                delete threadData[tid].curr;
//            }
//
//            if (threadData[tid].last) {
//                this->pool->addMoveAll(tid, threadData[tid].last);
//                delete threadData[tid].last;
//            }
//        }
    }

};
