#pragma once
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef DISABLE_PHYTM_BACKOFF
	#include <thread>
#endif

#include "../phytmCommon/common.h"

// extern volatile long CommitTallySW;

typedef struct Thread_void {
    PAD;
    long UniqID;
    volatile long Retries;
    int IsRO;
    int isFallback;
    long AbortsHW; // # of times hw txns aborted
    long AbortsSW; // # of times sw txns aborted
    long CommitsHW;
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    void* rdSet;
    void* wrSet;
    sigjmp_buf* envPtr;
    // int* txCounterRef;
    uint64_t readClock;
    bool sortWriteSet;
    PAD;
} Thread_void;


#include "../phytmCommon/stmUtil.h"

__INLINE__ uint64_t marsagliaXORV(uint64_t x) {
    if (x == 0) x = 1;
    x ^= x << 6;
    x ^= x >> 21;
    x ^= x << 7;
    return x;
}

__INLINE__ void backoff(Thread_void* Self) {
#ifndef DISABLE_PHYTM_BACKOFF
    if (Self->Retries < 3) return;
    Self->rng = marsagliaXORV(Self->rng);
    uint64_t stall = (Self->rng & Self->Retries) + 1;
    stall *= 16;
    stall = stall >= 10000 ? 10000 : stall;
    volatile uint64_t iter = 0;
    while (iter < stall) FAA(&iter, 1);
    if (stall > 1000) std::this_thread::yield();
#endif
}

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

#ifdef USE_GLOBAL_LOCK
#  define STM_BEGIN(stm_self) \
    do { \
        SOFTWARE_BARRIER; \
        STM_JMPBUF_T STM_JMPBUF; \
        \
        Thread_void* ___Self = (Thread_void*) (stm_self); \
        TxClearRWSets((stm_self)); \
        \
        XBEGIN_ARG_T ___xarg; \
        ___Self->Retries = 0; \
        ___Self->isFallback = 0; \
        ___Self->IsRO = 1; \
        ___Self->envPtr = &STM_JMPBUF; \
        unsigned ___htmattempts; \
        for (___htmattempts = 0; ___htmattempts < HTM_ATTEMPT_THRESH; ++___htmattempts) { \
            if (XBEGIN(___xarg)) { \
                if (lockflag) XABORT(HTM_GLOBAL_LOCK_ABORT_CODE); \
                break; \
            } else { /* if we aborted */ \
                ++___Self->AbortsHW; \
                TM_REGISTER_ABORT(PATH_FAST_HTM, ___xarg, ___Self->UniqID); \
                while (lockflag) { \
                    PAUSE(); \
                } \
            } \
        } \
        if (___htmattempts < HTM_ATTEMPT_THRESH) break; \
        /* STM attempt */ \
        SOFTWARE_BARRIER; \
        if (sigsetjmp(STM_JMPBUF, 0)) { \
            TxClearRWSets((stm_self)); \
        } \
        ___Self->isFallback = 1; \
        ___Self->IsRO = 1; \
        SYNC_RMW; /* prevent instructions in the txn/critical section from being moved before this point (on power) */ \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */
#else
#  define STM_BEGIN(stm_self) \
    do { \
        SOFTWARE_BARRIER; \
        STM_JMPBUF_T STM_JMPBUF; \
        \
        Thread_void* ___Self = (Thread_void*) (stm_self); \
        TxClearRWSets((stm_self)); \
        \
        XBEGIN_ARG_T ___xarg; \
        ___Self->Retries = 0; \
        ___Self->isFallback = 0; \
        ___Self->IsRO = 1; \
        ___Self->envPtr = &STM_JMPBUF; \
        unsigned ___htmattempts; \
        for (___htmattempts = 0; ___htmattempts < HTM_ATTEMPT_THRESH; ++___htmattempts) { \
            if (XBEGIN(___xarg)) { \
                break; \
            } else { /* if we aborted */ \
                /*++___Self->AbortsHW;*/ \
                TM_REGISTER_ABORT(PATH_FAST_HTM, ___xarg, ___Self->UniqID); \
            } \
        } \
        if (___htmattempts < HTM_ATTEMPT_THRESH) break; \
        /* STM attempt */ \
        SOFTWARE_BARRIER; \
        if (sigsetjmp(STM_JMPBUF, 0)) { \
            TxClearRWSets((stm_self)); \
            backoff(___Self); \
        } \
        ___Self->isFallback = 1; \
        ___Self->IsRO = 1; \
        SYNC_RMW; /* prevent instructions in the txn/critical section from being moved before this point (on power) */ \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */
#endif

typedef volatile intptr_t               vintp;
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(stm_self); TxMemManagerStart((stm_self));
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(stm_self); TxStart((stm_self));
#define STM_BEGIN_WR_STM_ONLY(stm_self, txCounterRef)          STM_BEGIN_STM_ONLY(0, stm_self, txCounterRef); TxStart((stm_self));
#define STM_END(stm_self)                             SOFTWARE_BARRIER; TxCommit((stm_self));

#define STM_READ_P(stm_self, var)       IP2VP(TxLoad((stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) TxStore((stm_self), (vintp*)(void*)&(var), VP2IP(val))

#ifndef DISALLOW_UNSAFE
    #define STM_UNSAFE_WRITE_P(stm_self, var, val) TxUnsafeStore((stm_self), (vintp*)(void*)&(var), VP2IP(val))
#endif