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
    int isFallback;
    int is_rh_active;
    int is_rh_prefix_active;
    int max_reads;
    int expected_length;
    int prefix_reads;
    int rh_prefix_retries;
//    long Starts; // how many times the user called TxBegin
    unsigned long long rng;
    unsigned long long xorrng [1];
    // void* allocPtr;    /* CCM: speculatively allocated */
    // void* freePtr;     /* CCM: speculatively free'd */
    // void* rdSet;
    // void* wrSet;
//    void* LocalUndo;
    sigjmp_buf* envPtr;
    int sequenceLock;
    PAD;
} Thread_void;

#include "rhnorec_post_pre.h"
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

__thread intptr_t (* sharedReadFunPtr)(void* Self, volatile intptr_t* addr);
__thread void (* sharedWriteFunPtr)(void* Self, volatile intptr_t* addr, intptr_t val);

#ifdef GSTATS_HANDLE_STATS
    #define GSTATS_ADD_FASTHTM_ABORT GSTATS_ADD(___Self->UniqID, fasthtm_abort, 1)
#else
    #define GSTATS_ADD_FASTHTM_ABORT
#endif

#ifdef GSTATS_HANDLE_STATS
    #define GSTATS_ADD_MIXED_ABORTS { \
        GSTATS_ADD(___Self->UniqID, htmprefix_abort, 1); \
        if (X_ABORT_STATUS_IS_USER(___xarg)) GSTATS_ADD(___Self->UniqID, htmprefix_abort_explicit, 1); \
        if (X_ABORT_STATUS_IS_CONFLICT(___xarg)) GSTATS_ADD(___Self->UniqID, htmprefix_abort_conflict, 1); \
        if (X_ABORT_STATUS_IS_CAPACITY(___xarg)) GSTATS_ADD(___Self->UniqID, htmprefix_abort_capacity, 1); \
        if (X_ABORT_STATUS_IS_NESTING(___xarg)) GSTATS_ADD(___Self->UniqID, htmprefix_abort_nesting, 1); \
        if (X_ABORT_STATUS_IS_ILLEGAL(___xarg)) GSTATS_ADD(___Self->UniqID, htmprefix_abort_illegal, 1); \
        if (___xarg == 0) GSTATS_ADD(___Self->UniqID, htmprefix_abort_zero, 1); \
    }
#else
    #define GSTATS_ADD_MIXED_ABORTS
#endif

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
        SOFTWARE_BARRIER; \
        sharedReadFunPtr = &TxLoad_htm; \
        sharedWriteFunPtr = &TxStore_htm; \
        SOFTWARE_BARRIER; \
        ___Self->isFallback = 0; \
        ___Self->IsRO = 1; \
        ___Self->is_rh_active = 0; \
        ___Self->is_rh_prefix_active = 0; \
        \
        int ___htmattempts; \
        /*printf("HTM_ATTEMPT_THRESH=%d\n", HTM_ATTEMPT_THRESH);*/ \
        for (___htmattempts = 0 ; ___htmattempts < HTM_ATTEMPT_THRESH; ++___htmattempts) { \
            /*printf("h/w loop iteration\n");*/ \
            while (global_htm_lock /*& 1*/) { PAUSE(); } \
            if (XBEGIN(___xarg)) { \
                if (global_htm_lock /*& 1*/) XABORT(0); \
                break; \
            } else { /* if we aborted */ \
                GSTATS_ADD_FASTHTM_ABORT; \
            } \
        } \
        /*printf("exited loop\n");*/ \
        if (___htmattempts < HTM_ATTEMPT_THRESH) break; \
        \
        \
        \
        /* mixed attempt with rh_prefix and rh_postfix */ \
        ___Self->rh_prefix_retries = 0; \
        if (sigsetjmp(STM_JMPBUF, 0)) { \
            TxClearRWSets((stm_self)); \
        } \
        SOFTWARE_BARRIER; \
        sharedReadFunPtr = &TxLoad_stm; \
        sharedWriteFunPtr = &TxStore_stm; \
        SOFTWARE_BARRIER; \
        ___Self->isFallback = 1; \
        ___Self->IsRO = 1; \
        ___Self->is_rh_active = 0; \
        ___Self->is_rh_prefix_active = 0; \
        for ( ; ___Self->rh_prefix_retries < HTM_ATTEMPT_THRESH; ++___Self->rh_prefix_retries) { \
            /*printf("h/w loop iteration\n");*/ \
            ___Self->max_reads = 5; \
            ___Self->prefix_reads = 0; \
            while (global_htm_lock /*& 1*/) { PAUSE(); } \
            if (XBEGIN(___xarg)) { \
                if (global_htm_lock /*& 1*/) XABORT(0); \
                ___Self->is_rh_prefix_active = 1; \
                break; \
            } else { /* if we aborted */ \
                /*___Self->expected_length = 0;*/ /* TODO: ACTUALLY ADAPT NUMBER OF READS BASED ON ABORT RATE, AS PER PAPER (SET INITIAL VALUE IN THREAD INIT...) */ \
                GSTATS_ADD_MIXED_ABORTS; \
            } \
        } \
        /*printf("exited loop\n");*/ \
        if (___Self->rh_prefix_retries < HTM_ATTEMPT_THRESH) break; \
        \
        \
        \
        /* mixed attempt with ONLY rh_postfix */ \
        assert(___Self->isFallback == 1); \
        assert(___Self->IsRO == 1); \
        assert(___Self->is_rh_active == 0); \
        assert(___Self->is_rh_prefix_active == 0); \
        __sync_fetch_and_add(&num_of_fallbacks, 1); \
        ___Self->sequenceLock = global_clock; \
        if (___Self->sequenceLock & 1) { \
            /*if (num_of_fallbacks > 500) printf("num_of_fallback=%d\n", num_of_fallbacks);*/ \
            SOFTWARE_BARRIER; \
            STM_RESTART(stm_self); \
        } \
        SOFTWARE_BARRIER; \
    } while (0); /* enforce comma */

typedef volatile intptr_t               vintp;
#define STM_BEGIN_RD(stm_self)          STM_BEGIN(1, stm_self)
#define STM_BEGIN_WR(stm_self)          STM_BEGIN(0, stm_self)
#define STM_END(stm_self)               SOFTWARE_BARRIER; TxCommit((stm_self))
#define STM_READ_P(stm_self, var)       IP2VP((*sharedReadFunPtr)((stm_self), (vintp*)(void*)&(var)))
#define STM_WRITE_P(stm_self, var, val) (*sharedWriteFunPtr)((stm_self), (vintp*)(void*)&(var), VP2IP(val))
