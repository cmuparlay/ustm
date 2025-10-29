#ifndef HYTM1_H
#define HYTM1_H 1

//#ifdef __cplusplus
//extern "C" {
//#endif

#define TM_NAME "HyTM1"
#ifndef HTM_ATTEMPT_THRESH
    #define HTM_ATTEMPT_THRESH 40
#endif
//#define TXNL_MEM_RECLAMATION

#define MAX_RETRIES 1000000

//#define DEBUG_PRINT
#define DEBUG_PRINT_LOCK

#include "counters/counters.h"
//extern tm_debugCounters<MAX_TID_POW2> __tm_counters;

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

#define BIG_CONSTANT(x) (x##LLU)






#include <stdint.h>
#include "platform.h"
// #include "tmalloc.h"

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

intptr_t TxLoad_htm(void* Self, volatile intptr_t* addr);
intptr_t TxLoad_stm(void* Self, volatile intptr_t* addr);
void TxStore_htm(void* Self, volatile intptr_t* addr, intptr_t value);
void TxStore_stm(void* Self, volatile intptr_t* addr, intptr_t value);

void     TxClearCounters();
void     TxOnce        ();
void     TxShutdown    ();

void*    TxAlloc       (void*, size_t);
void     TxFree        (void*, void*);

//#ifdef __cplusplus
//}
//#endif

#endif
