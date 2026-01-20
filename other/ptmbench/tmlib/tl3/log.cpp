#include "debug.h"
#include "log.h"

#ifdef USE_FULL_HASHTABLE
    void HashTable::validateInvariants() {
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

    __INLINE__ void HashTable::init(const long _sz) {
        // assert: _sz is a power of 2!
        DEBUG2 aout("hash table "<<renamePointer(this)<<" init");
        sz = 0;
        cap = 2 * _sz;
        data = (AVPair**) MALLOC_PADDED(sizeof(AVPair*) * cap);
        memset(data, 0, sizeof(AVPair*) * cap);
        VALIDATE_INV(this);
    }

    __INLINE__ void HashTable::destroy() {
        DEBUG2 aout("hash table "<<renamePointer(this)<<" destroy");
        FREE_PADDED(data);
    }

    __INLINE__ int32_t HashTable::hash(volatile intptr_t* addr) {
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

    __INLINE__ int32_t HashTable::findIx(volatile intptr_t* addr) {
        int32_t ix = hash(addr);
        while (data[ix]) {
            if (data[ix]->addr == addr) {
                return ix;
            }
            ix = (ix + 1) & (cap-1);
        }
        return -1;
    }

    __INLINE__ AVPair* HashTable::find(volatile intptr_t* addr) {
        int32_t ix = findIx(addr);
        if (ix < 0) return NULL;
        return data[ix];
    }

    // assumes there is space for e, and e is not in the hash table
    __INLINE__ void HashTable::insertFresh(AVPair* e) {
        DEBUG3 aout("hash table "<<renamePointer(this)<<" insertFresh("<<debug(e)<<")");
        VALIDATE_INV(this);
        int32_t ix = hash(e->addr);
        while (data[ix]) { // assumes hash table does NOT contain e
            ix = (ix + 1) & (cap-1);
        }
        data[ix] = e;
#ifdef HASHTABLE_CLEAR_FROM_LIST
        e->hashTableEntry = &data[ix];
//            e->hashTableIx = ix;
#endif
        ++sz;
        VALIDATE_INV(this);
    }

    __INLINE__ int HashTable::requiresExpansion() {
        return 2*sz > cap;
    }

    // expand table by a factor of 2
    __INLINE__ void HashTable::expandAndClear() {
        AVPair** olddata = data;
        init(cap); // note: cap will be doubled by init
        FREE_PADDED(olddata);
    }

    __INLINE__ void HashTable::expandAndRehashFromList(AVPair* head, AVPair* stop) {
        DEBUG2 aout("hash table "<<renamePointer(this)<<" expandAndRehashFromList");
        VALIDATE_INV(this);
        expandAndClear();
        for (AVPair* e = head; e != stop; e = e->Next) {
            insertFresh(e);
        }
        VALIDATE_INV(this);
    }

    __INLINE__ void HashTable::clear(AVPair* head, AVPair* stop) {
        sz = 0;
#ifdef HASHTABLE_CLEAR_FROM_LIST
        for (AVPair* e = head; e != stop; e = e->Next) {
            //assert(*e->hashTableEntry);
            //assert(*e->hashTableEntry == e);
            *e->hashTableEntry = NULL;
//                e->hashTableEntry = NULL;
//                assert(e->hashTableIx >= 0 && e->hashTableIx < cap);
//                assert(data[e->hashTableIx] == e);
//                data[e->hashTableIx] = 0;
        }
//            for (int i=0;i<cap;++i) {
//                assert(data[i] == 0);
//            }
#else
        memset(data, 0, sizeof(AVPair*) * cap);
#endif
    }

    void HashTable::validateContainsAllAndSameSize(AVPair* head, AVPair* stop, const int listsz) {
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

#elif defined(USE_BLOOM_FILTER)
    void HashTable::validateInvariants() {

    }

    __INLINE__ void HashTable::init() {
        for (unsigned i=0;i<BLOOM_FILTER_WORDS;++i) {
            filter[i] = 0;
        }
        VALIDATE_INV(this);
    }

    __INLINE__ void HashTable::destroy() {
        DEBUG2 aout("hash table "<<this<<" destroy");
    }

    __INLINE__ unsigned HashTable::hash(volatile intptr_t* key) {
        intptr_t p = (intptr_t) key;
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
        return p & (BLOOM_FILTER_BITS-1);
    }

    __INLINE__ bool HashTable::contains(volatile intptr_t* key) {
        unsigned targetBit = hash(key);
        bloom_filter_data_t fword = filter[targetBit / BLOOM_FILTER_DATA_T_BITS];
        return fword & (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
    }

    // assumes there is space for e, and e is not in the hash table
    __INLINE__ void HashTable::insertFresh(volatile intptr_t* key) {
        DEBUG3 aout("hash table "<<this<<" insertFresh("<<debug(key)<<")");
        VALIDATE_INV(this);
        unsigned targetBit = hash(key);
        unsigned wordix = targetBit / BLOOM_FILTER_DATA_T_BITS;
        assert(wordix >= 0 && wordix < BLOOM_FILTER_WORDS);
        filter[wordix] |= (1<<(targetBit & (BLOOM_FILTER_DATA_T_BITS-1))); // note: using x&(sz-1) where sz is a power of 2 as a shortcut for x%sz
        VALIDATE_INV(this);
    }
#else
    class HashTable {};
#endif

std::ostream& operator<<(std::ostream& out, const List& obj) {
    AVPair* stop = obj.put;
    for (AVPair* curr = obj.head; curr != stop; curr = curr->Next) {
        out<<*curr<<(curr->Next == stop ? "" : " ");
    }
    return out;
}

__INLINE__ AVPair* List::extendList() {
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

void List::validateInvariants() {
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

void List::init(Thread* Self, long _initcap) {
    DEBUG2 aout("list "<<renamePointer(this)<<" init");
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

void List::destroy() {
    DEBUG2 aout("list "<<renamePointer(this)<<" destroy");
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

__INLINE__ void List::clear() {
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

__INLINE__ AVPair* List::find(volatile intptr_t* addr) {
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

__INLINE__ AVPair* List::append(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, uint64_t _rdv) {
    AVPair* e = put;
    if (e == NULL) e = extendList();
    tail = e;
    put = e->Next;
    e->addr = addr;
    e->value = value;
    e->LockFor = _LockFor;
    e->rdv = _rdv;
    VALIDATE ++currsz;
    return e;
}

__INLINE__ void List::insertReplace(Thread* Self, volatile intptr_t* addr, intptr_t value, vLock* _LockFor, uint64_t _rdv) {
    DEBUG3 aout("list "<<renamePointer(this)<<" insertReplace("<<debug(renamePointer((const void*) (void*) addr))<<","<<debug(value)<<","<<debug(renamePointer(_LockFor))<<","<<debug(_rdv)<<")");
    AVPair* e = find(addr);
    if (e) {
        e->value = value;
    } else {
        e = append(Self, addr, value, _LockFor, _rdv);
#ifdef USE_FULL_HASHTABLE
        // insert in hash table
        tab.insertFresh(e);
        if (tab.requiresExpansion()) tab.expandAndRehashFromList(head, put);
#elif defined(USE_BLOOM_FILTER)
        tab.insertFresh(addr);
#endif
    }
}

// Transfer the data in the log to its ultimate location.
__INLINE__ void List::writeForward(Thread* Self) {
    DEBUG2 aout("thread "<<Self->UniqID<<" list "<<renamePointer(this)<<" writeForward: "<<*this);
    AVPair* stop = put;
    for (AVPair* e = head; e != stop; e = e->Next) {
        *e->addr = e->value;
    }
}

void List::validateContainsAllAndSameSize(HashTable* tab) {
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
