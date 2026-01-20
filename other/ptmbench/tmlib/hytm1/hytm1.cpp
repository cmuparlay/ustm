/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 *
 * This is essentially an implementation of TLE, but with an abort function
 * (including on the fallback path). To implement the abort function, we log
 * old values before writing.
 *
 * [ note: we cannot distribute this without inserting the appropriate
 *         copyright notices as required by TL2 and STAMP ]
 *
 * Authors: Trevor Brown (trevor.brown@uwaterloo.ca) and Srivatsan Ravi
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include "platform_impl.h"
#include "hytm1.h"
#include "stm.h"
// #include "tmalloc.h"
#include "util.h"
#include <iostream>
#include <execinfo.h>
#include <stdint.h>
using namespace std;

#ifndef PREFETCH_SIZE_BYTES
#define PREFETCH_SIZE_BYTES 192
#endif

#define USE_FULL_HASHTABLE
//#define USE_BLOOM_FILTER

#define HASHTABLE_CLEAR_FROM_LIST

#define MALLOC_PADDED(sz) ((void *) (((char *) malloc((sz) + 2*PREFETCH_SIZE_BYTES)) + PREFETCH_SIZE_BYTES))
#define FREE_PADDED(x) free((void *) (((char *) (x)) - PREFETCH_SIZE_BYTES))

// just for debugging
PAD;
volatile int globallock = 0; // for printf and cout

PAD;
volatile int tleLock = 0; // for tle software path
PAD;

void printStackTrace() {
  void *trace[16];
  char **messages = (char **)NULL;
  int i, trace_size = 0;

  trace_size = backtrace(trace, 16);
  messages = backtrace_symbols(trace, trace_size);
  /* skip first stack frame (points here) */
  printf("  [bt] Execution path:\n");
  for (i=1; i<trace_size; ++i)
  {
    printf("    [bt] #%d %s\n", i, messages[i]);

    /**
     * find first occurrence of '(' or ' ' in message[i] and assume
     * everything before that is the file name.
     */
    int p = 0; //size_t p = 0;
    while(messages[i][p] != '(' && messages[i][p] != ' '
            && messages[i][p] != 0)
        ++p;

    char syscom[256];
    sprintf(syscom,"echo \"    `addr2line %p -e %.*s`\"", trace[i], p, messages[i]);
        //last parameter is the file name of the symbol
    if (system(syscom) < 0) {
        printf("ERROR: could not run necessary command to build stack trace\n");
        exit(-1);
    };
  }

  exit(-1);
}

void initSighandler() {
    /* Install our signal handler */
    struct sigaction sa;

    sa.sa_handler = (sighandler_t) /*(void *)*/ printStackTrace;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;

    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGUSR1, &sa, NULL);
}

__INLINE__ void __acquireLock(volatile int *lock) {
    while (1) {
        if (*lock) {
            PAUSE();
            continue;
        }
        LWSYNC; // prevent the following CAS from being moved before read of lock (on power)
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            SYNC_RMW; // prevent instructions in the critical section from being moved before the lock (on power)
            return;
        }
    }
}

__INLINE__ void __releaseLock(volatile int *lock) {
    LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
    *lock = 0;
//    SYNC_RMW;
}












/**
 *
 * TRY-LOCK IMPLEMENTATION AND LOCK TABLE
 *
 */

PAD;
#include <map>
map<const void*, unsigned> addrToIx;
map<unsigned, const void*> ixToAddr;
volatile unsigned rename_ix = 0;
PAD;
#include <sstream>
string stringifyIndex(unsigned ix) {
#if 1
    const unsigned NCHARS = 36;
    stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        if (newchar < 10) {
            ss<<(char)(newchar+'0');
        } else {
            ss<<(char)((newchar-10)+'A');
        }
        ix /= NCHARS;
    }
    string backwards = ss.str();
    stringstream ssr;
    for (string::reverse_iterator rit = backwards.rbegin(); rit != backwards.rend(); ++rit) {
        ssr<<*rit;
    }
    return ssr.str();
#elif 0
    const unsigned NCHARS = 26;
    stringstream ss;
    if (ix == 0) return "0";
    while (ix > 0) {
        unsigned newchar = ix % NCHARS;
        ss<<(char)(newchar+'A');
        ix /= NCHARS;
    }
    return ss.str();
#else
    stringstream ss;
    ss<<ix;
    return ss.str();
#endif
}
string renamePointer(const void* p) {
    map<const void*, unsigned>::iterator it = addrToIx.find(p);
    if (it == addrToIx.end()) {
        unsigned newix = __sync_fetch_and_add(&rename_ix, 1);
        addrToIx[p] = newix;
        ixToAddr[newix] = p;
        return stringifyIndex(addrToIx[p]);
    } else {
        return stringifyIndex(it->second);
    }
}









/**
 *
 * THREAD CLASS
 *
 */

class List;

class Thread {
public:
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
    // tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    // tmalloc_t* freePtr;     /* CCM: speculatively free'd */
    List* rdSet;
    List* wrSet;
    sigjmp_buf* envPtr;
    PAD;

    Thread(long id);
    void destroy();
    void compileTimeAsserts() {
        CTASSERT(sizeof(*this) == sizeof(Thread_void));
    }
};















/**
 *
 * THREAD CLASS IMPLEMENTATION
 *
 */

PAD;
volatile long StartTally = 0;
volatile long AbortTallyHW = 0;
volatile long AbortTallySW = 0;
volatile long CommitTallyHW = 0;
volatile long CommitTallySW = 0;
PAD;

Thread::Thread(long id) {
    DEBUG1 aout("new thread with id "<<id);
    memset(this, 0, sizeof(Thread)); /* Default value for most members */
    UniqID = id;
    rng = id + 1;
    xorrng[0] = rng;

    // allocPtr = tmalloc_alloc(1);
    // freePtr = tmalloc_alloc(1);
    // assert(allocPtr);
    // assert(freePtr);
}

void Thread::destroy() {
}










/**
 *
 * IMPLEMENTATION OF TM OPERATIONS
 *
 */

void TxClearRWSets(void* _Self) {
}

inline int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;

    // software path
    if (Self->isFallback) {
        // release global lock
        TM_TIMER_END(Self->UniqID);
        __releaseLock(&tleLock);

// #ifdef TXNL_MEM_RECLAMATION
//         tmalloc_clear(Self->allocPtr);
//         tmalloc_releaseAllForward(Self->freePtr, NULL);
// #endif
        TM_COUNTER_INC(htmCommit[PATH_FALLBACK], Self->UniqID);

    // hardware path
    } else {
// #ifdef TXNL_MEM_RECLAMATION
//         tmalloc_clear(Self->allocPtr);
//         tmalloc_releaseAllForward(Self->freePtr, NULL);
// #endif
        XEND();
        TM_COUNTER_INC(htmCommit[PATH_FAST_HTM], Self->UniqID);
    }
    return true;
}

__attribute__((always_inline)) inline void TxAbort(void* _Self) {
    Thread* Self = (Thread*) _Self;
    XABORT(0);
}

__attribute__((always_inline)) inline intptr_t TxLoad_htm(void* _Self, volatile intptr_t* addr) {
    return *addr;
}

__attribute__((always_inline)) inline void TxStore_htm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    *addr = value;
}








/**
 *
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 *
 */

void TxOnce() {
//    initSighandler(); /**** DEBUG CODE ****/
    TM_CREATE_COUNTERS();
    printf("%s %s\n", TM_NAME, "system ready\n");
}

void TxClearCounters() {
    printf("Printing __tm_counters for %s and then clearing them in preparation for the real trial.\n", TM_NAME);
    TM_PRINT_COUNTERS();
    TM_CLEAR_COUNTERS();
    printf("Counters cleared.\n");
}

void TxShutdown() {
    printf("%s system shutdown:\n    HTM_ATTEMPT_THRESH=%d\n" //  Starts=%li CommitsHW=%li AbortsHW=%li CommitsSW=%li AbortsSW=%li\n"
                , TM_NAME
                , HTM_ATTEMPT_THRESH
                //CommitTallyHW+CommitTallySW,
                //CommitTallyHW, AbortTallyHW,
                //CommitTallySW, AbortTallySW
                );
    if (!__tm_counters) return; // didn't actually invoke TxOnce, so destructor not needed

    TM_PRINT_COUNTERS();
    TM_DESTROY_COUNTERS();
}

void* TxNewThread() {
    Thread* t = (Thread*) malloc(sizeof(Thread));
    assert(t);
    return t;
}

void TxFreeThread(void* _t) {
    Thread* t = (Thread*) _t;
    t->destroy();
    free(t);
}

void TxInitThread(void* _t, long id) {
    Thread* t = (Thread*) _t;
    *t = Thread(id);
}

/* =============================================================================
 * TxAlloc
 *
 * CCM: simple transactional memory allocation
 * =============================================================================
 */
void* TxAlloc(void* _Self, size_t size) {
// #ifdef TXNL_MEM_RECLAMATION
//     Thread* Self = (Thread*) _Self;
//     void* ptr = tmalloc_reserve(size);
//     if (ptr) {
//         tmalloc_append(Self->allocPtr, ptr);
//     }

//     return ptr;
// #else
//     return malloc(size);
// #endif
    return 0;
}

/* =============================================================================
 * TxFree
 *
 * CCM: simple transactional memory de-allocation
 * =============================================================================
 */
void TxFree(void* _Self, void* ptr) {
// #ifdef TXNL_MEM_RECLAMATION
//     Thread* Self = (Thread*) _Self;
//     tmalloc_append(Self->freePtr, ptr);
// #else
// //    free(ptr);
// #endif
}
