/* =============================================================================
 *
 * stm.h
 *
 * User program interface for STM. For an STM to interface with STAMP, it needs
 * to have its own stm.h for which it redefines the macros appropriately.
 *
 * =============================================================================
 *
 * Author: Chi Cao Minh
 *
 * =============================================================================
 *
 * Edited by Trevor Brown (trevor.brown@uwaterloo.ca)
 *
 * =============================================================================
 */

#pragma once
#include <setjmp.h>
#include <stdio.h>

extern volatile long CommitTallySW;

class List;

typedef struct Thread_void {
    PAD;
    long UniqID;
    volatile long Retries;
//    int* ROFlag; // not used by stamp
    int IsRO;
    int isFallback;
//    long Starts; // how many times the user called TxBegin
    long AbortsHW; // # of times hw txns aborted
    long AbortsSW; // # of times sw txns aborted
    long CommitsHW;
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    // void* allocPtr;    /* CCM: speculatively allocated */
    // void* freePtr;     /* CCM: speculatively free'd */
    List* rdSet;
    List* wrSet;
//    void* LocalUndo;
    sigjmp_buf* envPtr;
    int sequenceLock;
    PAD;
} Thread_void;

#include "hybridnorec.h"
#include "util.h"

#define STM_THREAD_T                    void
#define STM_SELF                        Self
#define STM_RO_FLAG                     ROFlag

#define STM_MALLOC(stm_self, size)      TxAlloc((stm_self), size) /*malloc(size)*/
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
        STM_JMPBUF_T STM_JMPBUF; \
        \
        XBEGIN_ARG_T ___xarg; \
        Thread_void* ___Self = (Thread_void*) (stm_self); \
        ___Self->Retries = 0; \
        ___Self->envPtr = &STM_JMPBUF; \
        \
        TxClearRWSets((stm_self)); \
        ___Self->isFallback = 0; \
        ___Self->IsRO = 1; \
        \
        int ___htmattempts; \
        /*printf("HTM_ATTEMPT_THRESH=%d\n", HTM_ATTEMPT_THRESH);*/ \
        for (___htmattempts = 0 ; ___htmattempts < HTM_ATTEMPT_THRESH; ++___htmattempts) { \
            /*printf("h/w loop iteration\n");*/ \
            while (esl /*& 1*/) { PAUSE(); } \
            if (XBEGIN(___xarg)) { \
                if (esl /*& 1*/) XABORT(0); \
                break; \
            } else { /* if we aborted */ \
                TM_REGISTER_ABORT(PATH_FAST_HTM, ___xarg, ___Self->UniqID); \
                ++___Self->AbortsHW; \
            } \
        } \
        /*printf("exited loop\n");*/ \
        if (___htmattempts < HTM_ATTEMPT_THRESH) break; \
        /*printf("passed h/w break\n");*/ \
        /* STM attempt */ \
        /*DEBUG2 aout("thread "<<___Self->UniqID<<" started s/w tx attempt "<<(___Self->AbortsSW+___Self->CommitsSW)<<"; s/w commits so far="<<___Self->CommitsSW);*/ \
        /*DEBUG1 if ((___Self->CommitsSW % 50000) == 0) aout("thread "<<___Self->UniqID<<" has committed "<<___Self->CommitsSW<<" s/w txns");*/ \
        DEBUG2 printf("thread %ld started s/w tx; attempts so far=%ld, s/w commits so far=%ld\n", ___Self->UniqID, (___Self->AbortsSW+___Self->CommitsSW), ___Self->CommitsSW); \
        DEBUG1 if ((___Self->CommitsSW % 100) == 0) printf("thread %ld has committed %ld s/w txns (over all threads so far=%ld)\n", ___Self->UniqID, ___Self->CommitsSW, CommitTallySW); \
        if (sigsetjmp(STM_JMPBUF, 0)) { \
            TxClearRWSets((stm_self)); \
        } \
        SOFTWARE_BARRIER; \
        ___Self->isFallback = 1; \
        ___Self->IsRO = 1; \
        do { \
            LWSYNC; /* prevent read of gsl from being moved before previous reads (on power) */ \
            ___Self->sequenceLock = gsl; \
            /*PAUSE();*/ \
        } while (___Self->sequenceLock & 1); \
        SYNC_RMW; /* prevent instructions in the txn/critical section from being moved before this point (on power) */ \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */

typedef volatile intptr_t               vintp;
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(1, stm_self)
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(0, stm_self)
#define STM_END(stm_self)               SOFTWARE_BARRIER; TxCommit((stm_self))
#define STM_READ_P(stm_self, var)       IP2VP(TxLoad((stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) TxStore((stm_self), (vintp*)(void*)&(var), VP2IP(val))
