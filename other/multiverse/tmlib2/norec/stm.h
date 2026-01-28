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

typedef struct Thread_void {
    PAD;
    long UniqID;
    volatile long Retries;
//    int* ROFlag; // not used by stamp
    int IsRO;
//    long Starts; // how many times the user called TxBegin
    long AbortsSW; // # of times sw txns aborted
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    // void* allocPtr;    /* CCM: speculatively allocated */
    // void* freePtr;     /* CCM: speculatively free'd */
    void* rdSet;
    void* wrSet;
//    void* LocalUndo;
    sigjmp_buf* envPtr;
    int sequenceLock;
    PAD;
} Thread_void;

#include "norec.h"
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
        sigsetjmp(STM_JMPBUF, 0); \
        SOFTWARE_BARRIER; \
        TxClearRWSets((stm_self)); \
        ___Self->IsRO = 1; \
        do { \
            LWSYNC; /* prevent read of gsl from being moved before previous reads (on power) */ \
            ___Self->sequenceLock = gsl; \
            /*PAUSE();*/ \
        } while (___Self->sequenceLock & 1); \
        /*TxStart((stm_self), &STM_JMPBUF, SETJMP_RETVAL, &STM_RO_FLAG);*/ \
        SYNC_RMW; /* prevent instructions in the txn/critical section from being moved before this point (on power) */ \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */

typedef volatile intptr_t               vintp;
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(1, stm_self)
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(0, stm_self)
#define STM_END(stm_self)               SOFTWARE_BARRIER; TxCommit((stm_self))
#define STM_READ_P(stm_self, var)       IP2VP(TxLoad_stm((stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) TxStore_stm((stm_self), (vintp*)(void*)&(var), VP2IP(val))
