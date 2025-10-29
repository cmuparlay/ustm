/**
 * Code for HyTM is loosely based on the code for TL2
 * (in particular, the data structures)
 *
 * This is an implementation of noREC.
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
#include "norec.h"
#include "../hytm1/platform_impl.h"
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
// #define HASHTABLE_CLEAR_FROM_SEQUENCE

#define MALLOC_PADDED(sz) ((void *) (((char *) malloc((sz) + 2*PREFETCH_SIZE_BYTES)) + PREFETCH_SIZE_BYTES))
#define FREE_PADDED(x) free((void *) (((char *) (x)) - PREFETCH_SIZE_BYTES))

// just for debugging
PAD;
volatile int globallock = 0;

PAD;
volatile int gsl = 0;
PAD;

enum ListType {
    READ_SET, WRITE_SET
};

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

class List;
//class TypeLogs;

class Thread {
public:
    PAD;
    long UniqID;
    volatile long Retries;
    int IsRO;
    long AbortsSW; // # of times sw txns aborted
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    // tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    // tmalloc_t* freePtr;     /* CCM: speculatively free'd */
    List* rdSet;
    List* wrSet;
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
//    union {
//        long l;
//#ifdef __LP64__
//        float f[2];
//#else
//        float f[1];
//#endif
//        intptr_t p;
//    } value;
//    vLock* LockFor;     /* points to the vLock covering addr */
//    vLockSnapshot rdv;  /* read-version @ time of 1st read - observed */
    long Ordinal;
#ifdef HASHTABLE_CLEAR_FROM_LIST
    AVPair** hashTableEntry;
#endif
//    int32_t hashTableIx;

    AVPair() {}
    AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal)
        : Next(_Next), Prev(_Prev), addr(0), value(0), Ordinal(_Ordinal)
#ifdef HASHTABLE_CLEAR_FROM_LIST
        , hashTableEntry(0)
#endif
    {}

    void validateInvariants() {

    }
} __attribute__((aligned(64)));

std::ostream& operator<<(std::ostream& out, const AVPair& obj) {
    return out<<"[addr="<<renamePointer((void*) obj.addr)
            //<<" val="<<obj.value.l
            //<<" lock@"<<renamePointer(obj.LockFor)
            //<<" prev="<<obj.Prev
            //<<" next="<<obj.Next
            //<<" ord="<<obj.Ordinal
            //<<" rdv="<<obj.rdv<<"@"<<(uintptr_t)(long*)&obj
            <<"]@"<<renamePointer(&obj);
}

enum hytm_config {
    INIT_WRSET_NUM_ENTRY = 128,
    INIT_RDSET_NUM_ENTRY = 1024,
    INIT_LOCAL_NUM_ENTRY = 1024,
};

struct HTCell {
    AVPair * node;
#ifdef HASHTABLE_CLEAR_FROM_SEQUENCE
    long seq;
#endif
};

#ifdef USE_FULL_HASHTABLE
    class HashTable {
    public:
PAD;
        HTCell * data;
        long sz;        // number of elements in the hash table
        long cap;       // capacity of the hash table
#ifdef HASHTABLE_CLEAR_FROM_SEQUENCE
        long seq;
#endif
        int __tid;
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
        // #ifdef LONG_VALIDATION
        //     // htabsz = size of hash table
        //     long _htabsz = 0;
        //     for (int i=0;i<cap;++i) {
        //         if (data[i]) {
        //             ++_htabsz; // # non-null entries of htab
        //         }
        //     }
        //     if (sz != _htabsz) {
        //         ERROR("hash table size incorrect: "<<debug(sz)<<" "<<debug(_htabsz));
        //     }
        // #endif
        }

    public:
        __INLINE__ void init(const long _sz, int tid) {
            __tid = tid;
            // assert: _sz is a power of 2!
            DEBUG3 aout("hash table "<<renamePointer(this)<<" init");
            sz = 0;
            cap = 2 * _sz;
            data = (HTCell *) MALLOC_PADDED(sizeof(HTCell) * cap);
            memset(data, 0, sizeof(HTCell) * cap);
            VALIDATE_INV(this);
            // printf("hash table capacity %ld\n", cap);
#ifdef GSTATS_HANDLE_STATS
            GSTATS_APPEND(__tid, ht_capacity, cap);
#endif
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
#ifdef HASHTABLE_CLEAR_FROM_SEQUENCE
            while (data[ix].node && data[ix].seq == seq) {
#else
            while (data[ix].node) {
#endif
                if (data[ix].node->addr == addr) {
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
            return data[ix].node;
        }

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(AVPair* e) {
            DEBUG3 aout("hash table "<<renamePointer(this)<<" insertFresh("<<debug(e)<<")");
            VALIDATE_INV(this);
            int32_t ix = hash(e->addr);
#ifdef HASHTABLE_CLEAR_FROM_SEQUENCE
            while (data[ix].node && data[ix].seq == seq) { // assumes hash table does NOT contain e
#else
            while (data[ix].node) {
#endif
                ix = (ix + 1) & (cap-1);
            }
            data[ix].node = e;
#ifdef HASHTABLE_CLEAR_FROM_LIST
            e->hashTableEntry = &data[ix].node;
//            e->hashTableIx = ix;
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
            HTCell * olddata = data;
            init(cap, __tid); // note: cap will be doubled by init
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
#ifdef HASHTABLE_CLEAR_FROM_LIST
            for (AVPair* e = head; e != stop; e = e->Next) {
                *e->hashTableEntry = NULL;
            }
#elif defined HASHTABLE_CLEAR_FROM_SEQUENCE
            ++seq;
#else
            memset(data, 0, sizeof(HTCell) * cap);
#endif
            sz = 0;
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
// #elif defined(USE_BLOOM_FILTER)
//     typedef unsigned long bloom_filter_data_t;
//     #define BLOOM_FILTER_DATA_T_BITS (sizeof(bloom_filter_data_t)*8)
//     #define BLOOM_FILTER_BITS 512
//     #define BLOOM_FILTER_WORDS (BLOOM_FILTER_BITS/sizeof(bloom_filter_data_t))
//     class HashTable {
//     public:
//         bloom_filter_data_t filter[BLOOM_FILTER_WORDS]; // bloom filter data
//     private:
//         void validateInvariants() {

//         }

//     public:
//         __INLINE__ void init() {
//             for (unsigned i=0;i<BLOOM_FILTER_WORDS;++i) {
//                 filter[i] = 0;
//             }
//             VALIDATE_INV(this);
//         }

//         __INLINE__ void destroy() {
//             DEBUG3 aout("hash table "<<this<<" destroy");
//         }

//         __INLINE__ unsigned hash(volatile intptr_t* key) {
//             intptr_t p = (intptr_t) key;
//     #ifdef __LP64__
//             p ^= p >> 33;
//             p *= BIG_CONSTANT(0xff51afd7ed558ccd);
//             p ^= p >> 33;
//             p *= BIG_CONSTANT(0xc4ceb9fe1a85ec53);
//             p ^= p >> 33;
//     #else
//             p ^= p >> 16;
//             p *= 0x85ebca6b;
//             p ^= p >> 13;
//             p *= 0xc2b2ae35;
//             p ^= p >> 16;
//     #endif
//             return p & (BLOOM_FILTER_BITS-1);
//         }

//         __INLINE__ bool contains(volatile intptr_t* key) {
//             unsigned targetBit = hash(key);
//             bloom_filter_data_t fword = filter[targetBit / BLOOM_FILTER_DATA_T_BITS];
//             return fword & (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
//         }

//         // assumes there is space for e, and e is not in the hash table
//         __INLINE__ void insertFresh(volatile intptr_t* key) {
//             DEBUG3 aout("hash table "<<this<<" insertFresh("<<debug(key)<<")");
//             VALIDATE_INV(this);
//             unsigned targetBit = hash(key);
//             unsigned wordix = targetBit / BLOOM_FILTER_DATA_T_BITS;
//             assert(wordix >= 0 && wordix < BLOOM_FILTER_WORDS);
//             filter[wordix] |= (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
//             VALIDATE_INV(this);
//         }
//     };
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

    ListType __listType;
    int __tid;
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
    void init(Thread* Self, long _initcap, ListType _listType, int _tid) {
        DEBUG3 aout("list "<<renamePointer(this)<<" init");
        __listType = _listType;
        __tid = _tid;
        // assert: _sz is a power of 2

        // Allocate the primary list as a large chunk so we can guarantee ascending &
        // adjacent addresses through the list. This improves D$ and DTLB behavior.
        head = (AVPair*) MALLOC_PADDED((sizeof (AVPair) * _initcap) + CACHE_LINE_SIZE);
        assert(head);
        memset(head, 0, sizeof(AVPair) * _initcap);
        AVPair* curr = head;
        put = head;
        end = NULL;
        tail = NULL;
        for (int i = 0; i < _initcap; i++) {
            AVPair* e = curr++;
#ifdef HASHTABLE_CLEAR_FROM_SEQUENCE
            *e = AVPair(curr, tail, i); // note: curr is invalid in the last iteration
#else
            *e = AVPair(curr, tail, i); // note: curr is invalid in the last iteration
#endif
            tail = e;
        }
        tail->Next = NULL; // fix invalid next pointer from last iteration
        initcap = _initcap;
        ovf = 0;
        currsz = 0;
        VALIDATE_INV(this);
#ifdef USE_FULL_HASHTABLE
        tab.init(_initcap, Self->UniqID);
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
        if (tab.sz) {
#   ifdef GSTATS_HANDLE_STATS
            if (__listType == READ_SET)     GSTATS_APPEND(__tid, tx_rdset_size, tab.sz);
            if (__listType == WRITE_SET)    GSTATS_APPEND(__tid, tx_wrset_size, tab.sz);
#   endif
        }
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
    __INLINE__ AVPair* append(Thread* Self, volatile intptr_t* addr, intptr_t value) {
        AVPair* e = put;
        if (e == NULL) e = extendList();
        tail = e;
        put = e->Next;
        e->addr = addr;
        e->value = value;
//        e->LockFor = _LockFor;
//        e->rdv = _rdv;
//        e->hashTableEntry = NULL;
        VALIDATE ++currsz;
        return e;
    }

public:
    __INLINE__ void insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value, bool onlyIfAbsent) {
        DEBUG3 aout("list "<<renamePointer(this)<<" insertReplace("<<debug(renamePointer((const void*) (void*) addr))<<","<<debug(value)<<")");
        AVPair* e = find(addr);
        if (e) {
            if (!onlyIfAbsent) e->value = value;
        } else {
            e = append(Self, addr, value);
#ifdef USE_FULL_HASHTABLE
            // insert in hash table
            tab.insertFresh(e);
            if (tab.requiresExpansion()) {
                tab.expandAndRehashFromList(head, put);
            }
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
// #ifdef USE_FULL_HASHTABLE
//         if (currsz != tab->sz) {
//             ERROR("hash table "<<debug(tab->sz)<<" has different size from list "<<debug(currsz));
//         }
//         AVPair* stop = put;
//         // each element of hash table appears in list
//         for (int i=0;i<tab->cap;++i) {
//             AVPair* elt = tab->data[i];
//             if (elt) {
//                 // element in hash table; is it in the list?
//                 bool found = false;
//                 for (AVPair* e = head; e != stop; e = e->Next) {
//                     if (e == elt) {
//                         found = true;
//                     }
//                 }
//                 if (!found) {
//                     ERROR("element "<<debug(*elt)<<" of hash table was not in list");
//                 }
//             }
//         }
// #endif
    }
};

std::ostream& operator<<(std::ostream& out, const List& obj) {
    AVPair* stop = obj.put;
    for (AVPair* curr = obj.head; curr != stop; curr = curr->Next) {
        out<<*curr<<(curr->Next == stop ? "" : " ");
    }
    return out;
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
    wrSet->init(this, INIT_WRSET_NUM_ENTRY, WRITE_SET, UniqID);
    rdSet->init(this, INIT_RDSET_NUM_ENTRY, READ_SET, UniqID);
    //LocalUndo->init(this, INIT_LOCAL_NUM_ENTRY);

    // allocPtr = tmalloc_alloc(1);
    // freePtr = tmalloc_alloc(1);
    // assert(allocPtr);
    // assert(freePtr);
}

void Thread::destroy() {
//    AtomicAdd((volatile intptr_t*)((void*) (&StartTally)), Starts);
    AtomicAdd((volatile intptr_t*)((void*) (&AbortTallySW)), AbortsSW);
    AtomicAdd((volatile intptr_t*)((void*) (&CommitTallySW)), CommitsSW);
    // tmalloc_free(allocPtr);
    // tmalloc_free(freePtr);
    wrSet->destroy();
    rdSet->destroy();
//    LocalUndo->destroy();
    free(wrSet);
    free(rdSet);
//    free(LocalUndo);
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
//    Self->LocalUndo->clear();
}

int validate(Thread* Self) {
    // the norec optimization: if the sequence number didn't change, then neither did any memory locations.
    // validation retry loop
    int currGSL;
    do {
        // wait until sequence lock is not held
        do {
            SYNC_RMW; // prevent first read of gsl from being moved before validation starts, and subsequent gsl reads being moved before this loop iteration, by the processor (on power) [todo: can this be LWSYNC?]
            currGSL = gsl; // volatile read, so compiler cannot optimize it out or move it
            //PAUSE();
        } while (currGSL & 1);
        // do value based validation
        LWSYNC; // prevent reads in validation from being moved before the read of gsl (on power)
        AVPair* stop = Self->rdSet->put;
        for (AVPair* curr = Self->rdSet->head; curr != stop; curr = curr->Next) {
            if (curr->value != *curr->addr) {
                DEBUG2 aout("thread "<<Self->UniqID<<" TxRead failed validation -> aborting (retries="<<Self->Retries<<")");
                TxAbort(Self);
            }
        }
        // if sequence lock has not changed, then our value based validation saw a snapshot
        LWSYNC; // prevent read of gsl from being moved before reads in validation (on power)
    } while (currGSL != gsl);
    return currGSL;
}

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;

    // return immediately if txn is read-only
    if (Self->IsRO) {
#ifdef GSTATS_HANDLE_STATS
        GSTATS_ADD(Self->UniqID, commit_tx_reader, 1);
#endif
        TM_COUNTER_INC(htmCommit[PATH_FALLBACK], Self->UniqID);
    } else {
        // acquire global sequence lock
        while (!__sync_bool_compare_and_swap(&gsl, Self->sequenceLock, Self->sequenceLock+1)) {
            // note: validate() starts with a SYNC_RMW barrier, so we don't need one just above this line
            Self->sequenceLock = validate(Self);
        }
        SYNC_RMW; // prevent instructions in the critical section from being moved before the lock (on power)
        assert((Self->sequenceLock & 1) == 0);
        assert((gsl & 1) == 1);
        TM_TIMER_START(Self->UniqID);

        // perform the actual writes
        Self->wrSet->writeForward();
        LWSYNC; // prevent gsl from being unlocked before writes (on POWER)
        gsl = Self->sequenceLock + 2;
        SOFTWARE_BARRIER;
        TM_TIMER_END(Self->UniqID);

#ifdef GSTATS_HANDLE_STATS
        GSTATS_ADD(Self->UniqID, commit_tx_writer, 1);
#endif
    }
    // ++Self->CommitsSW;
    TM_COUNTER_INC(htmCommit[PATH_FALLBACK], Self->UniqID);

    // TODO: how many of these are READ-ONLY???

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

    ++Self->Retries;
#ifdef GSTATS_HANDLE_STATS
    GSTATS_ADD(Self->UniqID, abort_tx, 1);
#endif

    // ++Self->AbortsSW;
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
    TM_REGISTER_ABORT(PATH_FALLBACK, 0, Self->UniqID);
    // TM_TIMER_END(Self->UniqID);
// #ifdef TXNL_MEM_RECLAMATION
//     // "abort" speculative allocations and speculative frees
//     tmalloc_releaseAllReverse(Self->allocPtr, NULL);
//     tmalloc_clear(Self->freePtr);
// #endif

    // longjmp to start of txn
    LWSYNC; // prevent any writes after the longjmp from being moved before this point (on power) // TODO: is this needed?
    SIGLONGJMP(*Self->envPtr, 1);
    ASSERT(0);
}

inline intptr_t TxLoad_stm(void* _Self, volatile intptr_t* addr) {
    Thread* Self = (Thread*) _Self;

    // check whether addr is in the write-set
    AVPair* av = Self->wrSet->find(addr);
    if (av) return av->value;

    // addr is NOT in the write-set, so we read it
    intptr_t val = *addr;
    LWSYNC; // prevent gsl from being read before *addr (on power)
    // validate reads
    while (Self->sequenceLock != gsl) {
        // note: validate begins with SYNC_RMW, so we don't need one here to prevent parts of validation from being moved before the read of gsl (on power)
        Self->sequenceLock = validate(Self);
        // note: validate ends with LWSYNC, so we don't need an LWSYNC here to prevent the read of *addr from being moved before the last read of gsl in validate (on power)
        val = *addr;
    }
    // add the value we read to the read-set
    // note: if addr changed, it was not changed by us (since we write only in commit)
    Self->rdSet->insertReplace(Self, addr, val, true);
    return val;
}

inline void TxStore_stm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;

    Self->IsRO = false; // txn is not read-only
    // add addr to the write-set
    Self->wrSet->insertReplace(Self, addr, value, false);
    // think through 2 writes
    // think through write then read
}










/**
 *
 * FRAMEWORK FUNCTIONS
 * (PROBABLY DON'T NEED TO BE CHANGED WHEN CREATING A VARIATION OF THIS TM)
 *
 */

void TxOnce() {
//    CTASSERT((_TABSZ & (_TABSZ - 1)) == 0); /* must be power of 2 */

//    initSighandler(); /**** DEBUG CODE ****/
    TM_CREATE_COUNTERS();
    printf("%s %s\n", TM_NAME, "system ready\n");
//    memset(LockTab, 0, _TABSZ*sizeof(vLock));
}

void TxClearCounters() {
    printf("Printing counters for %s and then clearing them in preparation for the real trial.\n", TM_NAME);
    TM_PRINT_COUNTERS();
    TM_CLEAR_COUNTERS();
    printf("Counters cleared.\n");
}

void TxShutdown() {
    printf("%s system shutdown:\n" //  Starts=%li CommitsHW=%li AbortsHW=%li CommitsSW=%li AbortsSW=%li\n"
                , TM_NAME
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
    return NULL;
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

