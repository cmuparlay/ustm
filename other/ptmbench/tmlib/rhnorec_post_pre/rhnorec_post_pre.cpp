/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 *
 * This is an implementation of Hybrid noREC with the optimization suggested in
 * the work on non-speculative operations in ASF.
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
#include "rhnorec_post_pre.h"
#include "../hytm1/platform_impl.h"
#include "stm.h"
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
volatile int globallock = 0; // DEBUG LOCK

PAD;
volatile int global_clock = 0;
PAD;
volatile int global_htm_lock = 0;
PAD;
volatile int num_of_fallbacks = 0;
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

void __acquireLock(volatile int *lock) {
    while (1) {
        if (*lock) {
            PAUSE();
            continue;
        }
        SYNC_RMW; // prevent the following CAS from being moved before read of lock (on power)
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            SYNC_RMW; // prevent instructions in the critical section from being moved before the lock (on power)
            return;
        }
    }
}

void __releaseLock(volatile int *lock) {
    LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
    *lock = 0;
}













/**
 *
 * TRY-LOCK IMPLEMENTATION AND LOCK TABLE
 *
 */

class Thread;

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

class Thread {
public:
    PAD;
    long UniqID;
    volatile long Retries;
    int IsRO;
    int isFallback;
    int is_rh_active;
    int is_rh_prefix_active;
    int max_reads;
    int expected_length;
    int prefix_reads;
    int rh_prefix_retries;
    unsigned long long rng;
    unsigned long long xorrng [1];
    // tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    // tmalloc_t* freePtr;     /* CCM: speculatively free'd */
    sigjmp_buf* envPtr;
    int sequenceLock;
    PAD;

    Thread(long id);
    void destroy();
    void compileTimeAsserts() {
        CTASSERT(sizeof(*this) == sizeof(Thread_void));
    }
};// __attribute__((aligned(CACHE_LINE_SIZE)));














/**
 *
 * THREAD CLASS IMPLEMENTATION
 *
 */

Thread::Thread(long id) {
    DEBUG1 aout("new thread with id "<<id);
    memset(this, 0, sizeof(Thread)); /* Default value for most members */
    UniqID = id;
    rng = id + 1;
    xorrng[0] = rng;
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

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;

    // software path
    if (Self->isFallback) {
        SOFTWARE_BARRIER; // prevent compiler reordering of speculative execution before isFallback check in htm (for power)
        // return immediately if txn is read-only

        if (Self->is_rh_prefix_active) {
            XEND();
#ifdef GSTATS_HANDLE_STATS
            GSTATS_ADD(Self->UniqID, htmprefix_commit, 1);
#endif

        } else if (Self->IsRO) {
            __sync_fetch_and_add(&num_of_fallbacks, -1);
#ifdef GSTATS_HANDLE_STATS
            GSTATS_ADD(Self->UniqID, stmprefix_commit, 1);
#endif

        } else {
            if (Self->is_rh_active) {
                XEND();
                Self->is_rh_active = 0;
#ifdef GSTATS_HANDLE_STATS
                GSTATS_ADD(Self->UniqID, htmpostfix_commit, 1);
#endif
            } else {
#ifdef GSTATS_HANDLE_STATS
                GSTATS_ADD(Self->UniqID, slow_commit, 1);
#endif
            }
            SOFTWARE_BARRIER;
            if (global_htm_lock) global_htm_lock = 0;
            SOFTWARE_BARRIER;
            global_clock = (global_clock & ~1) + 2;
            __sync_fetch_and_add(&num_of_fallbacks, -1);
        }

    // hardware path
    } else {
        if (!Self->IsRO) {
            if (num_of_fallbacks) {
                int seq = global_clock;
                if (seq & 1) {
                    XABORT(0); /* abort because global_clock was held */ // TxAbort(Self);
                }
                global_clock = seq + 2;
            }
        }
        XEND();
#ifdef GSTATS_HANDLE_STATS
        GSTATS_ADD(Self->UniqID, fasthtm_commit, 1);
#endif
    }

success:
// #ifdef TXNL_MEM_RECLAMATION
//     // "commit" speculative frees and speculative allocations
//     tmalloc_releaseAllForward(Self->freePtr, NULL);
//     tmalloc_clear(Self->allocPtr);
// #endif
    return true;
}

void TxAbort(void* _Self) {
    Thread* Self = (Thread*) _Self;

    // software path
    if (Self->isFallback) {
        SOFTWARE_BARRIER; // prevent compiler reordering of speculative execution before isFallback check in htm (for power)
        ++Self->Retries;
        if (Self->Retries > MAX_RETRIES) {
            aout("TOO MANY ABORTS. QUITTING.");
            aout("BEGIN DEBUG ADDRESS MAPPING:");
            __acquireLock(&globallock);
                for (unsigned i=0;i<rename_ix;++i) {
                    cout<<stringifyIndex(i)<<"="<<ixToAddr[i]<<" ";
                }
                cout<<endl;
            __releaseLock(&globallock);
            aout("END DEBUG ADDRESS MAPPING.");
            exit(-1);
        }
// #ifdef TXNL_MEM_RECLAMATION
//         // "abort" speculative allocations and speculative frees
//         tmalloc_releaseAllReverse(Self->allocPtr, NULL);
//         tmalloc_clear(Self->freePtr);
// #endif

        __sync_fetch_and_add(&num_of_fallbacks, -1);

        // longjmp to start of txn
#ifdef GSTATS_HANDLE_STATS
        GSTATS_ADD(Self->UniqID, stmprefix_abort, 1);
#endif
        LWSYNC; // prevent any writes after the longjmp from being moved before this point (on power) // TODO: is this needed?
        SIGLONGJMP(*Self->envPtr, 1);
        ASSERT(0);

    // hardware path
    } else {
        XABORT(0);
    }
}

void acquire_clock_lock(Thread * Self) {
    auto newval = Self->sequenceLock + 1;
    assert(newval & 1);
    auto expected = Self->sequenceLock;
    auto we_locked = __sync_bool_compare_and_swap(&global_clock, expected, newval);
    if (!we_locked) TxAbort(Self);
    Self->sequenceLock = newval;
}

bool start_rh_htm_postfix(Thread * Self) {
    int ___htm_postfix_attempts = 0;
    XBEGIN_ARG_T ___xarg;
    for (___htm_postfix_attempts = 0 ; ___htm_postfix_attempts < HTM_ATTEMPT_THRESH; ++___htm_postfix_attempts) {
        /*printf("htm postfix loop iteration\n");*/
        if (XBEGIN(___xarg)) {
            break;
        } else {
            /* if we aborted */
#ifdef GSTATS_HANDLE_STATS
            GSTATS_ADD(Self->UniqID, htmpostfix_abort, 1);
#endif
            if (X_ABORT_STATUS_IS_USER(___xarg)) {
                if (_XABORT_CODE(___xarg) == 42) {
                    TxAbort(Self);
                }
            }
        }
    }
    if (___htm_postfix_attempts < HTM_ATTEMPT_THRESH) {
        Self->is_rh_active = 1;
        return true;
    }
    return false;
}

void handle_first_write(Thread * Self) {
    Self->IsRO = 0; // txn is not read-only
    acquire_clock_lock(Self);
    if (!start_rh_htm_postfix(Self)) {
        global_htm_lock = 1;
    }
}

void commit_rh_htm_prefix(Thread * Self) {
    ++num_of_fallbacks;
    Self->IsRO = 1;
    Self->sequenceLock = global_clock;
    if (Self->sequenceLock & 1) {
        XABORT(2);
    }
    XEND();
    Self->is_rh_prefix_active = 0;
#ifdef GSTATS_HANDLE_STATS
    GSTATS_ADD(Self->UniqID, htmprefix_commit, 1);
#endif
}

inline intptr_t TxLoad_stm(void* _Self, volatile intptr_t* addr) {
    Thread* Self = (Thread*) _Self;

    if (Self->is_rh_prefix_active) {
        ++Self->prefix_reads;
        if (Self->prefix_reads < Self->max_reads) {
            return *addr;
        } else {
            commit_rh_htm_prefix(Self);
        }
    }

    intptr_t val = *addr;
    if (Self->sequenceLock != global_clock) {

        // restart(ctx):

        if (Self->is_rh_active) {
            ///////////////////////// this is tricky... going back only to postfix is a problem... need to completely restart
            XABORT(42); // 42 means abort THEN CALL TxAbort!
        } else {
            TxAbort(Self);
        }
    }
    return val;
}

inline intptr_t TxLoad_htm(void* _Self, volatile intptr_t* addr) {
    return *addr;
}

inline void TxStore_stm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;

    if (Self->is_rh_prefix_active) {
        commit_rh_htm_prefix(Self);
    }

    if (Self->IsRO) handle_first_write(Self);
    *addr = value;
}

inline void TxStore_htm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;
    Self->IsRO = false;
    *addr = value;
}










/**
 *
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 *
 */

void TxOnce() {
    printf("%s %s\n", TM_NAME, "system ready\n");
}

void TxClearCounters() {
}

void TxShutdown() {
    printf("%s system shutdown:\n    HTM_ATTEMPT_THRESH=%d\n"
                , TM_NAME
                , HTM_ATTEMPT_THRESH
    );
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

