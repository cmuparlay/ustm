#pragma once

//#ifdef __cplusplus
//extern "C" {
//#endif

#define TM_NAME "TL3"
#ifndef HTM_ATTEMPT_THRESH
    #define HTM_ATTEMPT_THRESH 40
#endif
#define MAX_RETRIES 1000000

#define BIG_CONSTANT(x) (x##LLU)

#define _XABORT_EXPLICIT_LOCKED 1

#define MALLOC_PADDED(sz) ((void *) (((char *) malloc((sz) + 2*PREFETCH_SIZE_BYTES)) + PREFETCH_SIZE_BYTES))
#define FREE_PADDED(x) free((void *) (((char *) (x)) - PREFETCH_SIZE_BYTES))

//#define TXNL_MEM_RECLAMATION


#include "util.h"
#include "debug.h"
#include "vlock.h"
#include "locktab.h"
#include "thread.h"
#include "avpair.h"
#include "log.h"

#include "../hytm1/counters/counters.h"

// just for debugging
extern volatile int globallock;






#include <stdint.h>
#include "../hytm1/platform.h"

#  include <setjmp.h>
#  define SIGSETJMP(env, savesigs)      sigsetjmp(env, savesigs)
#  define SIGLONGJMP(env, val)          siglongjmp(env, val); assert(0)

/*
 * Prototypes
 */

void     TxClearRWSets (void* _Self);
void     TxStart       (void*, sigjmp_buf*, int, int*);
void*    TxNewThread   ();

void     TxFreeThread  (void*);
void     TxInitThread  (void*, long id);
int      TxCommit      (void*);
void     TxAbort       (void*);

intptr_t TxLoad(void* Self, volatile intptr_t* addr);
intptr_t TxLoad_htm(void* Self, volatile intptr_t* addr);
intptr_t TxLoad_stm(void* Self, volatile intptr_t* addr);
void TxStore(void* Self, volatile intptr_t* addr, intptr_t value);
void TxStore_htm(void* Self, volatile intptr_t* addr, intptr_t value);
void TxStore_stm(void* Self, volatile intptr_t* addr, intptr_t value);

void     TxOnce        ();
void     TxClearCounters();
void     TxShutdown    ();

void*    TxAlloc       (void*, size_t);
void     TxFree        (void*, void*);

//#ifdef __cplusplus
//}
//#endif
