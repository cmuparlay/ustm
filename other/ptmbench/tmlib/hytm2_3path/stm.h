#pragma once

#define FAST_HTM_ATTEMPT_THRESH 20

#include <setjmp.h>
#include <stdio.h>

extern volatile long CommitTallySW;

typedef struct Thread_void {
    PAD;
    long UniqID;
    volatile long Retries;
    int IsRO;
    int isFallback;
    int isMiddle;
    long AbortsHW; // # of times hw txns aborted
    long AbortsSW; // # of times sw txns aborted
    long CommitsHW;
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    void* rdSet;
    void* wrSet;
    sigjmp_buf* envPtr;
    PAD;
} Thread_void;

#include "hytm2_3path.h"
#include "util.h"

#define STM_THREAD_T                    void
#define STM_SELF                        Self
#define STM_RO_FLAG                     ROFlag

#define STM_MALLOC(stm_self, size)      TxAlloc((stm_self), size)
#define STM_FREE(stm_self, ptr)         TxFree((stm_self), ptr)

#define STM_JMPBUF_T                    sigjmp_buf
#define STM_JMPBUF                      buf

#define STM_CLEAR_COUNTERS()            TxClearCounters()
#define STM_VALID()                     (1)
#define STM_RESTART(stm_self)           TxAbort((stm_self))

#define STM_STARTUP()                   TxOnce()
#define STM_SHUTDOWN()                  TxShutdown()

#define STM_NEW_THREAD(id)              TxNewThread()
#define STM_INIT_THREAD(t, id)          TxInitThread(t, id)
#define STM_FREE_THREAD(t)              TxFreeThread(t)

#  define STM_BEGIN(isReadOnly, stm_self) \
    do { \
        SOFTWARE_BARRIER; \
        STM_JMPBUF_T STM_JMPBUF; \
        /*int STM_RO_FLAG = isReadOnly;*/ \
        \
        Thread_void* ___Self = (Thread_void*) (stm_self); \
        TxClearRWSets((stm_self)); \
        \
        XBEGIN_ARG_T ___xarg; \
        ___Self->Retries = 0; \
        ___Self->isFallback = 0; \
        ___Self->isMiddle = 0; \
        ___Self->IsRO = 1; \
        ___Self->envPtr = &STM_JMPBUF; \
        /* fast htm attempts */ \
        unsigned ___fasthtmattempts; \
        for (___fasthtmattempts = 0; ___fasthtmattempts < FAST_HTM_ATTEMPT_THRESH; ++___fasthtmattempts) { \
            if (XBEGIN(___xarg)) { \
                if (fallbackCount) XABORT(1); \
                break; \
            } else { /* if we aborted */ \
                TM_REGISTER_ABORT(PATH_FAST_HTM, ___xarg, ___Self->UniqID); \
                ++___Self->AbortsHW; /* slightly undesirable: can't distinguish between fast and slow htm aborts */ \
                if (X_ABORT_STATUS_IS_USER(___xarg)) { /* GOTO SLOW HTM PATH */ \
                    /*TM_COUNTER_INC(jumpToSlow, ___Self->UniqID);*/ \
                    ___fasthtmattempts = FAST_HTM_ATTEMPT_THRESH; \
                    assert(fallbackCount <= MAX_TID_POW2); \
                } \
            } \
        } \
        if (___fasthtmattempts < FAST_HTM_ATTEMPT_THRESH) break; \
        /* slow (regular) htm attempts */ \
        ___Self->isMiddle = 1; \
        \
        unsigned ___slowhtmattempts; \
        for (___slowhtmattempts = 0; ___slowhtmattempts < HTM_ATTEMPT_THRESH; ++___slowhtmattempts) { \
            if (XBEGIN(___xarg)) { \
                break; \
            } else { /* if we aborted */ \
                TM_REGISTER_ABORT(PATH_SLOW_HTM, ___xarg, ___Self->UniqID); \
                ++___Self->AbortsHW; \
            } \
        } \
        /*printf("exited loop\n");*/ \
        if (___slowhtmattempts < HTM_ATTEMPT_THRESH) break; \
        /* STM attempt */ \
        \
        int oldFallbackCount = __sync_fetch_and_add(&fallbackCount, 1); \
        /*printf("thread %ld increments fallback to %d\n", ___Self->UniqID, (oldFallbackCount+1));*/ \
        \
        /*DEBUG2 aout("thread "<<___Self->UniqID<<" started s/w tx attempt "<<(___Self->AbortsSW+___Self->CommitsSW)<<"; s/w commits so far="<<___Self->CommitsSW);*/ \
        /*DEBUG1 if ((___Self->CommitsSW % 50000) == 0) aout("thread "<<___Self->UniqID<<" has committed "<<___Self->CommitsSW<<" s/w txns");*/ \
        DEBUG2 printf("thread %ld started s/w tx; attempts so far=%ld, s/w commits so far=%ld\n", ___Self->UniqID, (___Self->AbortsSW+___Self->CommitsSW), ___Self->CommitsSW); \
        DEBUG1 if ((___Self->CommitsSW % 25000) == 0) printf("thread %ld has committed %ld s/w txns (over all threads so far=%ld)\n", ___Self->UniqID, ___Self->CommitsSW, CommitTallySW); \
        SOFTWARE_BARRIER; \
        if (sigsetjmp(STM_JMPBUF, 0)) { \
            TM_REGISTER_ABORT(PATH_FALLBACK, 0, ___Self->UniqID); \
        } \
        TxClearRWSets((stm_self)); \
        ___Self->isMiddle = 0; \
        ___Self->isFallback = 1; \
        ___Self->IsRO = 1; \
        SYNC_RMW; /* prevent instructions in the txn/critical section from being moved before this point (on power) */ \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */

typedef volatile intptr_t               vintp;
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(1, stm_self)
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(0, stm_self)
#define STM_END(stm_self) \
    SOFTWARE_BARRIER; \
    TxCommit((stm_self)); \
    if (((Thread_void*) (stm_self))->isFallback) { \
        assert(fallbackCount > 0); \
        int oldFallbackCount = __sync_fetch_and_add(&fallbackCount, -1); \
        /*printf("thread %ld decrements fallback to %d\n", ((Thread_void*) (stm_self))->UniqID, (oldFallbackCount-1));*/ \
        TM_REGISTER_COMMIT(PATH_FALLBACK, ((Thread_void*) (stm_self))->UniqID); \
    } else if (((Thread_void*) (stm_self))->isMiddle) { \
        TM_REGISTER_COMMIT(PATH_SLOW_HTM, ((Thread_void*) (stm_self))->UniqID); \
    } else { \
        TM_REGISTER_COMMIT(PATH_FAST_HTM, ((Thread_void*) (stm_self))->UniqID); \
    }
#define STM_READ_P(stm_self, var)       IP2VP(TxLoad((stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) TxStore((stm_self), (vintp*)(void*)&(var), VP2IP(val))
