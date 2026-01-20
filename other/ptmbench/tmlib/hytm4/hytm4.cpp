/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 *
 * This is an implementation of Algorithm 2 from the paper
 * __MODIFIED__ to hold on to the global lock L eventually when aborting on the fallback path.
 *  ......... well, that WAS the original plan, but it was slow... so i tried switching to an MCS lock...
 *  that didn't really help either...
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
#include "hytm4.h"
#include "../hytm1/platform_impl.h"
#include "stm.h"
#include "util.h"
#include <iostream>
#include <execinfo.h>
#include <stdint.h>
#include "mcs.h"
using namespace std;

thread_local mcs_node my_mcs_node;
thread_local bool mcs_lock_is_mine = false;

#define MALLOC_PADDED(sz) ((void *) (((char *) malloc((sz) + 2*PREFETCH_SIZE_BYTES)) + PREFETCH_SIZE_BYTES))
#define FREE_PADDED(x) free((void *) (((char *) (x)) - PREFETCH_SIZE_BYTES))

#ifndef PREFETCH_SIZE_BYTES
#define PREFETCH_SIZE_BYTES 192
#endif

#define USE_FULL_HASHTABLE
//#define USE_BLOOM_FILTER

#define HASHTABLE_CLEAR_FROM_LIST

// just for debugging
PAD;
volatile int globallock = 0;

PAD;
volatile int lockflag = 0;
volatile int lockflag_owner = -1;
mcs_lock mcslock = NULL;
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

__INLINE__ bool __acquireLock(volatile int *lock, volatile int *owner, int tid) {
    if (owner && *owner == tid) return false;
    while (1) {
        if (*lock) {
            PAUSE();
            continue;
        }
        LWSYNC; // prevent the following CAS from being moved before read of lock (on power)
        if (__sync_bool_compare_and_swap(lock, 0, 1)) {
            if (owner) *owner = tid;
            SYNC_RMW; // prevent instructions in the critical section from being moved before the lock (on power)
            return true;
        }
    }
}

__INLINE__ void __releaseLock(volatile int *lock, volatile int *owner) {
    LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
    if (owner) *owner = -1;
    *lock = 0;
}












/**
 *
 * TRY-LOCK IMPLEMENTATION AND LOCK TABLE
 *
 */

class Thread;

#define LOCKBIT 1
class vLockSnapshot {
public:
//private:
    uint64_t lockstate;
public:
    __INLINE__ vLockSnapshot() {}
    __INLINE__ vLockSnapshot(uint64_t _lockstate) {
        lockstate = _lockstate;
    }
    __INLINE__ bool isLocked() const {
        return lockstate & LOCKBIT;
    }
    __INLINE__ uint64_t version() const {
//        cout<<"LOCKSTATE="<<lockstate<<" ~LOCKBIT="<<(~LOCKBIT)<<" VERSION="<<(lockstate & (~LOCKBIT))<<endl;
        return lockstate & (~LOCKBIT);
    }
    friend std::ostream& operator<<(std::ostream &out, const vLockSnapshot &obj);
};

class vLock {
    union {
        struct {
            volatile uint64_t lock; // (Version,LOCKBIT)
            volatile void* volatile owner; // invariant: NULL when lock is not held; non-NULL (and points to thread that owns lock) only when lock is held (but sometimes may be NULL when lock is held). guarantees that a thread can tell if IT holds the lock (but cannot necessarily tell who else holds the lock).
        };
//        char bytes[PREFETCH_SIZE_BYTES];
    };
private:
    __INLINE__ vLock(uint64_t lockstate) {
        lock = lockstate;
        owner = 0;
    }
public:
    __INLINE__ vLock() {
        lock = 0;
        owner = NULL;
    }
    __INLINE__ vLockSnapshot getSnapshot() const {
        vLockSnapshot retval (lock);
//        __sync_synchronize();
        return retval;
    }
    __INLINE__ void release(void* thread) {
        // note: even without a membar here, the read of owner cannot be moved before our last lock acquisition (the global lock), after which the owner cannot change (for power)
        if (thread == owner) { // reentrant release (assuming the lock should be released on the innermost release() call)
            owner = NULL; // cannot be moved earlier by a processor without violating single-thread consistency
            SOFTWARE_BARRIER;
            LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
            ++lock;
        }
    }

    __INLINE__ bool glockedAcquire(void* thread, vLockSnapshot& oldval) {
//        if (thread == owner) return true; // reentrant acquire
        // note: even without a membar here, nothing below can be moved before our last lock acquisition (on power)
        if (oldval.version() == lock) {
            ++lock; // cannot be moved before the above read of lock by a processor without violating single-threaded consistency (on power)
            SOFTWARE_BARRIER;
            LWSYNC; // prevent assignment of owner from being moved before lock acquisition (on power)
            owner = thread;
            SYNC_RMW; // prevent instructions in the critical section from being moved before lock acquisition or owner assignment (on power)
            return true;
        }
        SYNC_RMW; // prevent instructions in the critical section from being moved before lock acquisition or owner assignment (on power)
        return false;
    }

    // can be invoked only by a hardware transaction
    __INLINE__ void htmIncrementVersion() {
        lock += 2;
    }

    __INLINE__ bool isOwnedBy(void* thread) {
        return (thread == owner);
    }

    friend std::ostream& operator<<(std::ostream &out, const vLock &obj);
};

#include <map>
PAD;
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

std::ostream& operator<<(std::ostream& out, const vLockSnapshot& obj) {
    return out<<"ver="<<obj.version()
                <<",locked="<<obj.isLocked()
                ;//<<"> (raw="<<obj.lockstate<<")";
}

std::ostream& operator<<(std::ostream& out, const vLock& obj) {
    return out<<"<"<<obj.getSnapshot()<<",owner="<<(obj.owner?((Thread_void*) obj.owner)->UniqID:-1)<<">@"<<renamePointer(&obj);
}

/*
 * Consider 4M alignment for LockTab so we can use large-page support.
 * Alternately, we could mmap() the region with anonymous DZF pages.
 */
#define _TABSZ  (1<<20)
PAD;
static vLock LockTab[_TABSZ];
PAD;

/*
 * With PS the versioned lock words (the LockTab array) are table stable and
 * references will never fault.  Under PO, however, fetches by a doomed
 * zombie txn can fault if the referent is free()ed and unmapped
 */
#if 0
#define LDLOCK(a)                     LDNF(a)  /* for PO */
#else
#define LDLOCK(a)                     *(a)     /* for PS */
#endif

/*
 * PSLOCK: maps variable address to lock address.
 * For PW the mapping is simply (UNS(addr)+sizeof(int))
 * COLOR attempts to place the lock(metadata) and the data on
 * different D$ indexes.
 */
#define TABMSK (_TABSZ-1)
#define COLOR 0 /*(128)*/

/*
 * ILP32 vs LP64.  PSSHIFT == Log2(sizeof(intptr_t)).
 */
#define PSSHIFT ((sizeof(void*) == 4) ? 2 : 3)
#define PSLOCK(a) (LockTab + (((UNS(a)+COLOR) >> PSSHIFT) & TABMSK)) /* PS1M */










/**
 *
 * THREAD CLASS
 *
 */

class List;
//class TypeLogs;

class Thread {
public:
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
    // tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    // tmalloc_t* freePtr;     /* CCM: speculatively free'd */
//    TypeLogs* rdSet;
//    TypeLogs* wrSet;
//    TypeLogs* LocalUndo;
    List* rdSet;
    List* wrSet;
    sigjmp_buf* envPtr;
PAD;

    Thread(long id);
    void destroy();
    void compileTimeAsserts() {
        CTASSERT(sizeof(*this) == sizeof(Thread_void));
    }
};// __attribute__((aligned(CACHE_LINE_SIZE)));













/**
 *
 * LOG IMPLEMENTATION
 *
 */

/* list element (serves as an entry in a read/write set) */
class AVPair {
public:
    AVPair* Next;
    AVPair* Prev;
    volatile intptr_t* addr;
    intptr_t value;
    vLock* LockFor;     /* points to the vLock covering addr */
    vLockSnapshot rdv;  /* read-version @ time of 1st read - observed */
    long Ordinal;
    AVPair** hashTableEntry;

    AVPair() {}
    AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal)
        : Next(_Next), Prev(_Prev), addr(0), value(0), LockFor(0), rdv(0), Ordinal(_Ordinal), hashTableEntry(0)
    {}

    void validateInvariants() {}
} __attribute__((aligned(64)));

std::ostream& operator<<(std::ostream& out, const AVPair& obj) {
    return out<<"[addr="<<renamePointer((void*) obj.addr)
            //<<" val="<<obj.value.l
            <<" lock@"<<renamePointer(obj.LockFor)
            //<<" prev="<<obj.Prev
            //<<" next="<<obj.Next
            //<<" ord="<<obj.Ordinal
            //<<" rdv="<<obj.rdv<<"@"<<(uintptr_t)(long*)&obj
            <<"]@"<<renamePointer(&obj);
}

enum hytm_config {
    INIT_WRSET_NUM_ENTRY = 128,
    INIT_RDSET_NUM_ENTRY = 1024,
    INIT_LOCAL_NUM_ENTRY = 64,
};

#ifdef USE_FULL_HASHTABLE
    class HashTable {
    public:
PAD;
        AVPair** data;
        long sz;        // number of elements in the hash table
        long cap;       // capacity of the hash table
PAD;
    private:
        void validateInvariants() {
            // hash table capacity is a power of 2
            long htc = cap;
            while (htc > 0) {
                if ((htc & 1) && (htc != 1)) {
                    ERROR(debug(cap)<<" is not a power of 2");
                }
                htc /= 2;
            }
            // htabcap >= 2*htabsz
            if (requiresExpansion()) {
                ERROR("hash table capacity too small: "<<debug(cap)<<" "<<debug(sz));
            }
        #ifdef LONG_VALIDATION
            // htabsz = size of hash table
            long _htabsz = 0;
            for (int i=0;i<cap;++i) {
                if (data[i]) {
                    ++_htabsz; // # non-null entries of htab
                }
            }
            if (sz != _htabsz) {
                ERROR("hash table size incorrect: "<<debug(sz)<<" "<<debug(_htabsz));
            }
        #endif
        }

    public:
        __INLINE__ void init(const long _sz) {
            // assert: _sz is a power of 2!
            DEBUG3 aout("hash table "<<renamePointer(this)<<" init");
            sz = 0;
            cap = 2 * _sz;
            data = (AVPair**) MALLOC_PADDED(sizeof(AVPair*) * cap);
            memset(data, 0, sizeof(AVPair*) * cap);
            VALIDATE_INV(this);
        }

        __INLINE__ void destroy() {
            DEBUG3 aout("hash table "<<renamePointer(this)<<" destroy");
            FREE_PADDED(data);
        }

        __INLINE__ int32_t hash(volatile intptr_t* addr) {
            intptr_t p = (intptr_t) addr;
            // assert: htabcap is a power of 2
    #ifdef __LP64__
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xff51afd7ed558ccd);
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
            p ^= p >> 33;
    #else
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            assert(0 <= (p & (cap-1)) && (p & (cap-1)) < INT32_MAX);
            return p & (cap-1);
        }

        __INLINE__ int32_t findIx(volatile intptr_t* addr) {
//            int k = 0;
            int32_t ix = hash(addr);
            while (data[ix]) {
                if (data[ix]->addr == addr) {
                    return ix;
                }
                ix = (ix + 1) & (cap-1);
//                ++k; if (k > 100*cap) { exit(-1); } // TODO: REMOVE THIS DEBUG CODE: catch infinite loops
            }
            return -1;
        }

        __INLINE__ AVPair* find(volatile intptr_t* addr) {
            int32_t ix = findIx(addr);
            if (ix < 0) return NULL;
            return data[ix];
        }

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(AVPair* e) {
            DEBUG3 aout("hash table "<<renamePointer(this)<<" insertFresh("<<debug(e)<<")");
            VALIDATE_INV(this);
            int32_t ix = hash(e->addr);
            while (data[ix]) { // assumes hash table does NOT contain e
                ix = (ix + 1) & (cap-1);
            }
            data[ix] = e;
#ifdef HASHTABLE_CLEAR_FROM_LIST
            e->hashTableEntry = &data[ix];
#endif
            ++sz;
            VALIDATE_INV(this);
        }

        __INLINE__ int requiresExpansion() {
            return 2*sz > cap;
        }

    private:
        // expand table by a factor of 2
        __INLINE__ void expandAndClear() {
            AVPair** olddata = data;
            init(cap); // note: cap will be doubled by init
            FREE_PADDED(olddata);
        }

    public:
        __INLINE__ void expandAndRehashFromList(AVPair* head, AVPair* stop) {
            DEBUG3 aout("hash table "<<renamePointer(this)<<" expandAndRehashFromList");
            VALIDATE_INV(this);
            expandAndClear();
            for (AVPair* e = head; e != stop; e = e->Next) {
                insertFresh(e);
            }
            VALIDATE_INV(this);
        }

        __INLINE__ void clear(AVPair* head, AVPair* stop) {
            sz = 0;
#ifdef HASHTABLE_CLEAR_FROM_LIST
            for (AVPair* e = head; e != stop; e = e->Next) {
                //assert(*e->hashTableEntry);
                //assert(*e->hashTableEntry == e);
                *e->hashTableEntry = NULL;
//                e->hashTableEntry = NULL;
            }
#else
            memset(data, 0, sizeof(AVPair*) * cap);
#endif
        }

        void validateContainsAllAndSameSize(AVPair* head, AVPair* stop, const int listsz) {
            // each element of list appears in hash table
            for (AVPair* e = head; e != stop; e = e->Next) {
                if (find(e->addr) != e) {
                    ERROR("element "<<debug(*e)<<" of list was not in hash table");
                }
            }
            if (listsz != sz) {
                ERROR("list and hash table sizes differ: "<<debug(listsz)<<" "<<debug(sz));
            }
        }
    };
#elif defined(USE_BLOOM_FILTER)
    typedef unsigned long bloom_filter_data_t;
    #define BLOOM_FILTER_DATA_T_BITS (sizeof(bloom_filter_data_t)*8)
    #define BLOOM_FILTER_BITS 512
    #define BLOOM_FILTER_WORDS (BLOOM_FILTER_BITS/sizeof(bloom_filter_data_t))
    class HashTable {
    public:
PAD;
        bloom_filter_data_t filter[BLOOM_FILTER_WORDS]; // bloom filter data
PAD;
    private:
        void validateInvariants() {

        }

    public:
        __INLINE__ void init() {
            for (unsigned i=0;i<BLOOM_FILTER_WORDS;++i) {
                filter[i] = 0;
            }
            VALIDATE_INV(this);
        }

        __INLINE__ void destroy() {
            DEBUG3 aout("hash table "<<this<<" destroy");
        }

        __INLINE__ unsigned hash(volatile intptr_t* key) {
    #ifdef __LP64__
            int64_t p = (int64_t) key;
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xff51afd7ed558ccd);
            p ^= p >> 33;
            p *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
            p ^= p >> 33;
    #else
            int32_t p = (int32_t) key;
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            return p & (BLOOM_FILTER_BITS-1);
        }

        __INLINE__ bool contains(volatile intptr_t* key) {
            unsigned targetBit = hash(key);
            bloom_filter_data_t fword = filter[targetBit / BLOOM_FILTER_DATA_T_BITS];
            return fword & (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
        }

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(volatile intptr_t* key) {
            DEBUG3 aout("hash table "<<this<<" insertFresh("<<debug(key)<<")");
            VALIDATE_INV(this);
            unsigned targetBit = hash(key);
            unsigned wordix = targetBit / BLOOM_FILTER_DATA_T_BITS;
            assert(wordix >= 0 && wordix < BLOOM_FILTER_WORDS);
            filter[wordix] |= (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
            VALIDATE_INV(this);
        }
    };
#else
    class HashTable {};
#endif

class List {
public:
    // linked list (for iteration)
PAD;
    AVPair* head;
    AVPair* put;    /* Insert position - cursor */
    AVPair* tail;   /* CCM: Pointer to last valid entry */
    AVPair* end;    /* CCM: Pointer to last entry */
    long ovf;       /* Overflow - request to grow */
    long initcap;
    long currsz;

    HashTable tab;
PAD;

private:
    __INLINE__ AVPair* extendList() {
        VALIDATE_INV(this);
        // Append at the tail. We want the front of the list,
        // which sees the most traffic, to remains contiguous.
        ovf++;
        AVPair* e = (AVPair*) malloc(sizeof(*e));
        assert(e);
        tail->Next = e;
        *e = AVPair(NULL, tail, tail->Ordinal+1);
        end = e;
        VALIDATE_INV(this);
        return e;
    }

    void validateInvariants() {
        // currsz == size of list
        long _currsz = 0;
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            VALIDATE_INV(e);
            ++_currsz;
        }
        if (currsz != _currsz) {
            ERROR("list size incorrect: "<<debug(currsz)<<" "<<debug(_currsz));
        }

        // capacity is correct and next fields are not too far
        long _currcap = 0;
        for (AVPair* e = head; e; e = e->Next) {
            VALIDATE_INV(e);
            if (e->Next > head+initcap && ovf == 0) {
                ERROR("list has AVPair with a next field that jumps off the end of the AVPair array, but ovf is 0; "<<debug(*e));
            }
            if (e->Next && e->Next != e+1) {
                ERROR("list has incorrect distance between AVPairs; "<<debug(e)<<" "<<debug(e->Next));
            }
            ++_currcap;
        }
        if (_currcap != initcap) {
            ERROR("list capacity incorrect: "<<debug(initcap)<<" "<<debug(_currcap));
        }
    }
public:
    void init(Thread* Self, long _initcap) {
        DEBUG3 aout("list "<<renamePointer(this)<<" init");
        // assert: _sz is a power of 2

        // Allocate the primary list as a large chunk so we can guarantee ascending &
        // adjacent addresses through the list. This improves D$ and DTLB behavior.
        head = (AVPair*) MALLOC_PADDED(sizeof(AVPair) * _initcap);
        assert(head);
        memset(head, 0, sizeof(AVPair) * _initcap);
        AVPair* curr = head;
        put = head;
        end = NULL;
        tail = NULL;
        for (int i = 0; i < _initcap; i++) {
            AVPair* e = curr++;
            *e = AVPair(curr, tail, i); // note: curr is invalid in the last iteration
            tail = e;
        }
        tail->Next = NULL; // fix invalid next pointer from last iteration
        initcap = _initcap;
        ovf = 0;
        currsz = 0;
        VALIDATE_INV(this);
#ifdef USE_FULL_HASHTABLE
        tab.init(_initcap);
#elif defined(USE_BLOOM_FILTER)
        tab.init();
#endif
    }

    void destroy() {
        DEBUG3 aout("list "<<renamePointer(this)<<" destroy");
        /* Free appended overflow entries first */
        AVPair* e = end;
        if (e != NULL) {
            while (e->Ordinal >= initcap) {
                AVPair* tmp = e;
                e = e->Prev;
                free(tmp);
            }
        }
        /* Free contiguous beginning */
        FREE_PADDED(head);
#if defined(USE_FULL_HASHTABLE) || defined(USE_BLOOM_FILTER)
        tab.destroy();
#endif
    }

    __INLINE__ void clear() {
        DEBUG3 aout("list "<<renamePointer(this)<<" clear");
        VALIDATE_INV(this);
#ifdef USE_FULL_HASHTABLE
        tab.clear(head, put);
#elif defined(USE_BLOOM_FILTER)
        tab.init();
#endif
        put = head;
        tail = NULL;
        currsz = 0;
        VALIDATE_INV(this);
    }

    __INLINE__ AVPair* find(volatile intptr_t* addr) {
#ifdef USE_FULL_HASHTABLE
        return tab.find(addr);
#elif defined(USE_BLOOM_FILTER)
        if (!tab.contains(addr)) return NULL;
#endif
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            if (e->addr == addr) {
                return e;
            }
        }
        return NULL;
    }

private:
    __INLINE__ AVPair* append(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, vLockSnapshot _rdv) {
        AVPair* e = put;
        if (e == NULL) e = extendList();
        tail = e;
        put = e->Next;
        e->addr = addr;
        e->value = value;
        e->LockFor = _LockFor;
        e->rdv = _rdv;
//        e->hashTableEntry = NULL;
        VALIDATE ++currsz;
        return e;
    }

public:
    __INLINE__ void insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, vLockSnapshot _rdv) {
        DEBUG3 aout("list "<<renamePointer(this)<<" insertReplace("<<debug(renamePointer((const void*) (void*) addr))<<","<<debug(value)<<","<<debug(renamePointer(_LockFor))<<","<<debug(_rdv)<<")");
        AVPair* e = find(addr);
        if (e) {
            e->value = value;
        } else {
            e = append(Self, addr, value, _LockFor, _rdv);
#ifdef USE_FULL_HASHTABLE
            // insert in hash table
            tab.insertFresh(e);
            if (tab.requiresExpansion()) tab.expandAndRehashFromList(head, put); // TODO: enable table expansion
#elif defined(USE_BLOOM_FILTER)
            tab.insertFresh(addr);
#endif
        }
    }

    // Transfer the data in the log to its ultimate location.
    __INLINE__ void writeForward() {
        //DEBUG3 aout("list "<<renamePointer(this)<<" writeForward");
        AVPair* stop = put;
        for (AVPair* e = head; e != stop; e = e->Next) {
            *e->addr = e->value;
        }
    }

    void validateContainsAllAndSameSize(HashTable* tab) {
#ifdef USE_FULL_HASHTABLE
        if (currsz != tab->sz) {
            ERROR("hash table "<<debug(tab->sz)<<" has different size from list "<<debug(currsz));
        }
        AVPair* stop = put;
        // each element of hash table appears in list
        for (int i=0;i<tab->cap;++i) {
            AVPair* elt = tab->data[i];
            if (elt) {
                // element in hash table; is it in the list?
                bool found = false;
                for (AVPair* e = head; e != stop; e = e->Next) {
                    if (e == elt) {
                        found = true;
                    }
                }
                if (!found) {
                    ERROR("element "<<debug(*elt)<<" of hash table was not in list");
                }
            }
        }
#endif
    }
};

std::ostream& operator<<(std::ostream& out, const List& obj) {
    AVPair* stop = obj.put;
    for (AVPair* curr = obj.head; curr != stop; curr = curr->Next) {
        out<<*curr<<(curr->Next == stop ? "" : " ");
    }
    return out;
}

__INLINE__ vLockSnapshot* minLockSnapshot(vLockSnapshot* a, vLockSnapshot* b) {
    if (a && b) {
        return (a->version() < b->version()) ? a : b;
    } else {
        return a ? a : b; // note: might return NULL
    }
}

__INLINE__ vLockSnapshot* getMinLockSnap(AVPair* a, AVPair* b) {
    if (a && b) {
        return minLockSnapshot(&a->rdv, &b->rdv);
    } else {
        return a ? &a->rdv : (b ? &b->rdv : NULL);
    }
}

// can be invoked only by a transaction on the software path
/*__INLINE__*/
bool lockAll(Thread* Self, List* lockAVPairs) {
    DEBUG3 aout("lockAll "<<*lockAVPairs);
    assert(Self->isFallback);

    AVPair* const stop = lockAVPairs->put;
    for (AVPair* curr = lockAVPairs->head; curr != stop; curr = curr->Next) {
        if (!curr->LockFor->isOwnedBy(Self)) {
            /**
             * note: there is a full membar at the end of every iteration of
             * this loop, since one is implied by the lock acquisition attempt.
             *
             * no extra membars are needed here (on power), because isOwnedBy
             * is thread local, and instructions below can be moved before it
             * only if doing so would not violate sequential consistency
             * (meaning the result of isOwnedBy() cannot change).
             */

            // determine when we first encountered curr->addr in txload or txstore.
            // curr->rdv contains when we first encountered it in a txSTORE,
            // so we also need to compare it with any rdv stored in the READ-set.
            AVPair* readLogEntry = Self->rdSet->find(curr->addr);
            vLockSnapshot *encounterTime = getMinLockSnap(curr, readLogEntry);

            if (!curr->LockFor->glockedAcquire(Self, *encounterTime)) {
                for (AVPair* toUnlock = lockAVPairs->head; toUnlock != curr; toUnlock = toUnlock->Next) {
                    toUnlock->LockFor->release(Self);
                    DEBUG2 aout("thread "<<Self->UniqID<<" releasing lock "<<*curr->LockFor<<" in lockAll (will be ver "<<(curr->LockFor->getSnapshot().version()+2)<<")");
                }
                return false;
            }
        }
        DEBUG2 aout("thread "<<Self->UniqID<<" acquired lock "<<*curr->LockFor);
    }
    return true;
}

__INLINE__ bool glockedAcquireWS(Thread* Self) {
//    return lockAll(Self, &Self->wrSet->locks.list);
//    return lockAll<long>(Self, &Self->wrSet->l.addresses.list)
//        && lockAll<float>(Self, &Self->wrSet->f.addresses.list);
    return lockAll(Self, Self->wrSet);
}

// NOT TO BE INVOKED DIRECTLY
// releases all locks on addresses in a Log
/*__INLINE__*/ void releaseAll(Thread* Self, List* lockAVPairs) {
    DEBUG3 aout("releaseAll "<<*lockAVPairs);
    AVPair* const stop = lockAVPairs->put;
    for (AVPair* curr = lockAVPairs->head; curr != stop; curr = curr->Next) {
        DEBUG2 aout("thread "<<Self->UniqID<<" releasing lock "<<*curr->LockFor<<" in releaseAll (will be ver "<<(curr->LockFor->getSnapshot().version()+2)<<")");
        // note: any necessary membars here are implied by the lock release
        curr->LockFor->release(Self);
    }
}

// can be invoked only by a transaction on the software path
// releases all locks on addresses in the write-set
__INLINE__ void releaseWriteSet(Thread* Self) {
    assert(Self->isFallback);
//    releaseAll(Self, &Self->wrSet->locks.list);
//    releaseAll(Self, &Self->wrSet->l.addresses.list);
//    releaseAll(Self, &Self->wrSet->f.addresses.list);
    releaseAll(Self, Self->wrSet);
}

// can be invoked only by a transaction on the software path.
// writeSet must point to the write-set for this Thread that
// contains addresses/values of type T.
__INLINE__ bool validateLockVersions(Thread* Self, List* lockAVPairs/*, bool holdingLocks*/) {
    DEBUG3 aout("validateLockVersions "<<*lockAVPairs);//<<" "<<debug(holdingLocks));
    assert(Self->isFallback);

    AVPair* const stop = lockAVPairs->put;
    for (AVPair* curr = lockAVPairs->head; curr != stop; curr = curr->Next) {
        vLock* lock = curr->LockFor;
        vLockSnapshot locksnap = lock->getSnapshot();
        LWSYNC; // prevent any following reads from being reordered before getSnapshot()
        if (locksnap.isLocked()) {
            if (lock->isOwnedBy(Self)) {
                continue; // we own it
            } else {
                return false; // someone else locked it
            }
        } else {
            // determine when we first encountered curr->addr in txload or txstore.
            // curr->rdv contains when we first encountered it in a txLOAD,
            // so we also need to compare it with any rdv stored in the WRITE-set.
            AVPair* writeLogEntry = Self->wrSet->find(curr->addr);
            vLockSnapshot *encounterTime = getMinLockSnap(curr, writeLogEntry);
            assert(encounterTime);

            if (locksnap.version() != encounterTime->version()) {
                // the address is locked, and its version number has changed
                // (and we didn't change it, since we don't hold the lock)
                return false; // abort if we are not holding any locks
            }
        }
    }
    return true;
}

__INLINE__ bool validateReadSet(Thread* Self/*, bool holdingLocks*/) {
//    return validateLockVersions(Self, &Self->rdSet->locks.list, holdingLocks);
//    return validateLockVersions<long>(Self, &Self->rdSet->l.addresses.list)
//        && validateLockVersions<float>(Self, &Self->rdSet->f.addresses.list);
    return validateLockVersions(Self, Self->rdSet);
}

__INLINE__ intptr_t AtomicAdd(volatile intptr_t* addr, intptr_t dx) {
    intptr_t v;
    for (v = *addr; CAS(addr, v, v + dx) != v; v = *addr) {}
    return (v + dx);
}










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

    wrSet = (List*) malloc(sizeof(*wrSet));//(TypeLogs*) malloc(sizeof(TypeLogs));
    rdSet = (List*) malloc(sizeof(*rdSet));//(TypeLogs*) malloc(sizeof(TypeLogs));
    //LocalUndo = (TypeLogs*) malloc(sizeof(TypeLogs));
    wrSet->init(this, INIT_WRSET_NUM_ENTRY);
    rdSet->init(this, INIT_RDSET_NUM_ENTRY);
    //LocalUndo->init(this, INIT_LOCAL_NUM_ENTRY);

    // allocPtr = tmalloc_alloc(1);
    // freePtr = tmalloc_alloc(1);
    // assert(allocPtr);
    // assert(freePtr);
}

void Thread::destroy() {
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallySW)), AbortsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallyHW)), AbortsHW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallySW)), CommitsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallyHW)), CommitsHW);
    // tmalloc_free(allocPtr);
    // tmalloc_free(freePtr);
    wrSet->destroy();
    rdSet->destroy();
    free(wrSet);
    free(rdSet);
}










/**
 *
 * IMPLEMENTATION OF TM OPERATIONS
 *
 */

void TxClearRWSets(void* _Self) {
    Thread* Self = (Thread*) _Self;
    Self->wrSet->clear();
    Self->rdSet->clear();
    // tmalloc_clear(Self->allocPtr);
    // tmalloc_clear(Self->freePtr);
}

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;

    // software path
    if (Self->isFallback) {
        SOFTWARE_BARRIER; // prevent compiler reordering of speculative execution before isFallback check in htm (for power)

        // return immediately if txn is read-only
        if (Self->IsRO) {
            DEBUG2 aout("thread "<<Self->UniqID<<" commits read-only txn");
            //++Self->CommitsSW;
            goto success;
        }

        // acquire global STM lock
        // if (!mcs_lock_is_mine) {
            mcs_acquire(&mcslock, &my_mcs_node);
            // mcs_lock_is_mine = true;
            TM_TIMER_START(Self->UniqID);
        // }

        // if (__acquireLock(&lockflag, &lockflag_owner, Self->UniqID)) {
        //     // if we acquired it newly (rather than already holding it)
        //     // then start the timer for "seconds global lock is held"
            // TM_TIMER_START(Self->UniqID);
        // }

        // lock all addresses in the write-set, then validate the read-set
        DEBUG2 aout("thread "<<Self->UniqID<<" invokes glockedAcquireWS "<<*Self->wrSet);
        if (!glockedAcquireWS(Self)) {
            DEBUG2 aout("thread "<<Self->UniqID<<" TxCommit failed glockedAcquireWS -> release global lock & abort");

            // if (Self->Retries > 0) { // if we've already failed on the fallback path, WE DON'T GIVE UP THE GLOBAL LOCK
            // } else {
                TM_TIMER_END(Self->UniqID);
                // __releaseLock(&lockflag, &lockflag_owner);
                // mcs_lock_is_mine = false;
                mcs_release(&mcslock, &my_mcs_node);
            // }

            TxAbort(Self);
        }
        if (!validateReadSet(Self)) {
            // release all locks and abort
            DEBUG2 aout("thread "<<Self->UniqID<<" TxCommit failed validation -> release locks & abort");
            releaseWriteSet(Self);

            // if (Self->Retries > 0) { // if we've already failed on the fallback path, WE DON'T GIVE UP THE GLOBAL LOCK
            // } else {
                TM_TIMER_END(Self->UniqID);
                // __releaseLock(&lockflag, &lockflag_owner);
                // mcs_lock_is_mine = false;
                mcs_release(&mcslock, &my_mcs_node);
            // }

            TxAbort(Self);
        }

        // perform the actual writes
        LWSYNC; // prevent writes from being done before any validation (on power)
        Self->wrSet->writeForward();

        // release all locks
        DEBUG2 aout("thread "<<Self->UniqID<<" committed -> release locks");
        releaseWriteSet(Self);
        TM_TIMER_END(Self->UniqID);
        // __releaseLock(&lockflag, &lockflag_owner);
        // mcs_lock_is_mine = false;
        mcs_release(&mcslock, &my_mcs_node);
        //++Self->CommitsSW;
        TM_COUNTER_INC(htmCommit[PATH_FALLBACK], Self->UniqID);

    // hardware path
    } else {
        XEND();
        //++Self->CommitsHW;
        TM_COUNTER_INC(htmCommit[PATH_FAST_HTM], Self->UniqID);
    }

success:
// #ifdef TXNL_MEM_RECLAMATION
// #error DEBUG CODE
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
        ++Self->Retries; // retries ON THE FALLBACK PATH
        //++Self->AbortsSW;
        if (Self->Retries > MAX_RETRIES) {
            aout("TOO MANY ABORTS. QUITTING.");
            aout("BEGIN DEBUG ADDRESS MAPPING:");
            __acquireLock(&globallock, NULL, Self->UniqID);
                for (unsigned i=0;i<rename_ix;++i) {
                    cout<<stringifyIndex(i)<<"="<<ixToAddr[i]<<" ";
                }
                cout<<endl;
            __releaseLock(&globallock, NULL);
            aout("END DEBUG ADDRESS MAPPING.");
            exit(-1);
        }
        TM_REGISTER_ABORT(PATH_FALLBACK, 0, Self->UniqID);
        TM_TIMER_END(Self->UniqID);

// #ifdef TXNL_MEM_RECLAMATION
//         // "abort" speculative allocations and speculative frees
//         // Rollback any memory allocation, and longjmp to retry the txn
//         tmalloc_releaseAllReverse(Self->allocPtr, NULL);
//         tmalloc_clear(Self->freePtr);
// #endif

        // longjmp to start of txn
        LWSYNC; // prevent any writes after the longjmp from being moved before this point (on power) // TODO: is this needed?
        SIGLONGJMP(*Self->envPtr, 1);
        ASSERT(0);

    // hardware path
    } else {
        XABORT(0);
    }
}

inline intptr_t TxLoad(void * _Self, volatile intptr_t * addr) {
    Thread* Self = (Thread*) _Self;
    if (Self->isFallback) {
        // check whether addr is in the write-set
        AVPair* av = Self->wrSet->find(addr);
        if (av) return av->value;//unpackValue(av);

        // // acquire global STM lock
        // if (Self->Retries > 10 && __acquireLock(&lockflag, &lockflag_owner, Self->UniqID)) {
        //     // if we acquired it newly (rather than already holding it)
        //     // then start the timer for "seconds global lock is held"
        //     TM_TIMER_START(Self->UniqID);
        // }

        // // acquire global STM lock
        // if (Self->Retries > 10 && !mcs_lock_is_mine) {
        //     mcs_acquire(&mcslock, &my_mcs_node);
        //     mcs_lock_is_mine = true;
        //     // if we acquired it newly (rather than already holding it)
        //     // then start the timer for "seconds global lock is held"
        //     TM_TIMER_START(Self->UniqID);
        // }

        // addr is NOT in the write-set; abort if it is locked
        vLock* lock = PSLOCK(addr);
        vLockSnapshot locksnap = lock->getSnapshot();
        if (locksnap.isLocked()) {// && !lock->isOwnedBy(Self)) { // impossible for us to hold a lock on it.
            DEBUG2 aout("thread "<<Self->UniqID<<" TxRead saw lock "<<renamePointer(lock)<<" was held (not by us) -> aborting (retries="<<Self->Retries<<")");
            // if (mcs_lock_is_mine) {
            //     mcs_lock_is_mine = false;
            //     mcs_release(&mcslock, &my_mcs_node);
            // }
            TxAbort(Self);
        }

        // read addr and add it to the read-set
        LWSYNC; // prevent read of addr from being moved before read of its lock (on power)
        intptr_t val = *addr;
        Self->rdSet->insertReplace(Self, addr, val, lock, locksnap);

        // validate reads
        LWSYNC; // prevent reads of locks in validation from being moved before the read of addr (on power)
        if (!validateReadSet(Self)) {
            DEBUG2 aout("thread "<<Self->UniqID<<" TxRead failed validation -> aborting (retries="<<Self->Retries<<")");
            // if (mcs_lock_is_mine) {
            //     mcs_lock_is_mine = false;
            //     mcs_release(&mcslock, &my_mcs_node);
            // }
            TxAbort(Self);
        }
        return val;
    } else {
        return *addr;
    }
}

inline void TxStore(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;
    if (Self->isFallback) {
        // check whether addr is in the write-set
        AVPair* av = Self->wrSet->find(addr); // searching for this twice is somewhat inefficient sometimes... but shouldn't be a big deal if writes are somewhat rare
        if (!av) {
            // addr is NOT in the write-set; have we read it yet?
            // if not, we MUST, to satisfy Trevor's assumptions / constraints on usage...
            if (!Self->rdSet->find(addr)) {
                TxLoad(_Self, addr);
            }
        }

        // abort if addr is locked (since we do not hold any locks)
        vLock* lock = PSLOCK(addr);
        vLockSnapshot locksnap = lock->getSnapshot();
        // add addr to the write-set
        Self->wrSet->insertReplace(Self, addr, value, lock, locksnap);
        Self->IsRO = false; // txn is not read-only
    } else {
        // increment version number (to notify s/w txns of the change)
        // and write value to addr (order unimportant because of h/w)
        vLock* lock = PSLOCK(addr);
        lock->htmIncrementVersion();
        *addr = value;
    }
}










/**
 *
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 *
 */

void TxOnce() {
    CTASSERT((_TABSZ & (_TABSZ - 1)) == 0); /* must be power of 2 */

//    initSighandler(); /**** DEBUG CODE ****/
    TM_CREATE_COUNTERS();
    printf("%s %s\n", TM_NAME, "system ready\n");
    memset(LockTab, 0, _TABSZ*sizeof(vLock));
}

void TxClearCounters() {
    printf("Printing counters for %s and then clearing them in preparation for the real trial.\n", TM_NAME);
    TM_PRINT_COUNTERS();
    TM_CLEAR_COUNTERS();
    printf("Counters cleared.\n");
}

void TxShutdown() {
    printf("%s system shutdown:\n    HTM_ATTEMPT_THRESH=%d\n", TM_NAME, HTM_ATTEMPT_THRESH);
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
