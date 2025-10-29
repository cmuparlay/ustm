#pragma once

#include "../tm.h"
#include "avpair.h"

#define USE_FULL_HASHTABLE
//#define USE_BLOOM_FILTER

#define HASHTABLE_CLEAR_FROM_LIST

#define VALIDATE_INV(x) VALIDATE (x)->validateInvariants()
#define VALIDATE if(0)

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
        void validateInvariants();

    public:
        __INLINE__ void init(const long _sz);
        __INLINE__ void destroy();
        __INLINE__ int32_t hash(volatile intptr_t* addr);
        __INLINE__ int32_t findIx(volatile intptr_t* addr);
        __INLINE__ AVPair* find(volatile intptr_t* addr);

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(AVPair* e);
        __INLINE__ int requiresExpansion();

    private:
        // expand table by a factor of 2
        __INLINE__ void expandAndClear();

    public:
        __INLINE__ void expandAndRehashFromList(AVPair* head, AVPair* stop);
        __INLINE__ void clear(AVPair* head, AVPair* stop);
        void validateContainsAllAndSameSize(AVPair* head, AVPair* stop, const int listsz);
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
        void validateInvariants();

    public:
        __INLINE__ void init();
        __INLINE__ void destroy();
        __INLINE__ unsigned hash(volatile intptr_t* key);
        __INLINE__ bool contains(volatile intptr_t* key);

        // assumes there is space for e, and e is not in the hash table
        __INLINE__ void insertFresh(volatile intptr_t* key);
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
    __INLINE__ AVPair* extendList();
    void validateInvariants();
public:
    void init(Thread* Self, long _initcap);
    void destroy();
    __INLINE__ void clear();
    __INLINE__ AVPair* find(volatile intptr_t* addr);
private:
    __INLINE__ AVPair* append(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, uint64_t _rdv);
public:
    __INLINE__ void insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, uint64_t _rdv);
    __INLINE__ void writeForward(Thread* Self);
    void validateContainsAllAndSameSize(HashTable* tab);
};
