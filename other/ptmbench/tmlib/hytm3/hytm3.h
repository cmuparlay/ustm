#pragma once

//#ifdef __cplusplus
//extern "C" {
//#endif

#define TM_NAME "HyTM3"
#ifndef HTM_ATTEMPT_THRESH
    #ifndef DISABLE_HTM
        #define HTM_ATTEMPT_THRESH 40
    #else
        #define HTM_ATTEMPT_THRESH 0
    #endif
#endif
//#define TXNL_MEM_RECLAMATION

#define MAX_RETRIES 1000000

#include "../hytm1/counters/counters.h"

//#define DEBUG_PRINT
#define DEBUG_PRINT_LOCK

#ifdef DEBUG_PRINT
    #define aout(x) { \
        cout<<x<<endl; \
    }
#elif defined(DEBUG_PRINT_LOCK)
    #define aout(x) { \
        __acquireLock(&globallock); \
        cout<<x<<endl; \
        __releaseLock(&globallock); \
    }
#else
    #define aout(x)
#endif

#define debug(x) (#x)<<"="<<x
//#define LONG_VALIDATION
#define VALIDATE_INV(x) VALIDATE (x)->validateInvariants()
#define VALIDATE if(0)
#define ERROR(x) { \
    cerr<<"ERROR: "<<x<<endl; \
    printStackTrace(); \
    exit(-1); \
}

// just for debugging
extern volatile int globallock;

extern volatile int lockflag;

#define BIG_CONSTANT(x) (x##LLU)






#include <stdint.h>
#include "../hytm1/platform.h"

#  include <setjmp.h>
#  define SIGSETJMP(env, savesigs)      sigsetjmp(env, savesigs)
#  define SIGLONGJMP(env, val)          siglongjmp(env, val); assert(0)

/*
 * Prototypes
 */

void     TxClearRWSets (void* _Self);
void*    TxNewThread   ();

void     TxFreeThread  (void*);
void     TxInitThread  (void*, long id);
int      TxCommit      (void*);
void     TxAbort       (void*);

intptr_t TxLoad(void* Self, volatile intptr_t* addr);
// intptr_t TxLoad_htm(void* Self, volatile intptr_t* addr);
// intptr_t TxLoad_stm(void* Self, volatile intptr_t* addr);
void TxStore(void* Self, volatile intptr_t* addr, intptr_t value);
// void TxStore_htm(void* Self, volatile intptr_t* addr, intptr_t value);
// void TxStore_stm(void* Self, volatile intptr_t* addr, intptr_t value);

void     TxOnce        ();
void     TxClearCounters();
void     TxShutdown    ();

void*    TxAlloc       (void*, size_t);
void     TxFree        (void*, void*);

//#ifdef __cplusplus
//}
//#endif
