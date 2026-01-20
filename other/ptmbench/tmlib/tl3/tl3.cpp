/**
 * Code is loosely based on the code for TL2
 * (in particular, the data structures)
 *
 * Trevor Brown (trevor.brown@uwaterloo.ca)
 */

#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <signal.h>
#include <iostream>
#include <execinfo.h>
#include <stdint.h>

#include "tl3.h"
#include "../hytm1/platform_impl.h"
#include "stm.h"


using namespace std;


/*
 * Consider 4M alignment for LockTab so we can use large-page support.
 * Alternately, we could mmap() the region with anonymous DZF pages.
 */
#define _TABSZ  (1<<20) /*(1<<3)*/
#define STACK_SPACE_LOCKTAB
#ifdef STACK_SPACE_LOCKTAB
PAD;
static vLock LockTab[_TABSZ];
PAD;
#else
struct vLockSpace {
    char buf[sizeof(vLock)];
};
PAD;
static vLockSpace *LockTab;
PAD;
#endif

#define LDLOCK(a)                     *(a)     /* for PS */
#define TABMSK (_TABSZ-1)
#define COLOR 0 /*(128)*/
#define PSSHIFT ((sizeof(void*) == 4) ? 2 : 3)
#define PSLOCK(a) ((vLock*) (LockTab + (((UNS(a)+COLOR) >> PSSHIFT) & TABMSK))) /* PS1M */























__INLINE__ uint64_t getMinLockSnap(AVPair* a, AVPair* b) {
    if (a && b) {
        return (a->rdv < b->rdv ? a->rdv : b->rdv);
    } else {
        return a ? a->rdv : (b ? b->rdv : 0x7fffffffffffffffLL);
    }
}

// can be invoked only by a transaction on the software path
/*__INLINE__*/
bool lockAll(Thread* Self, List* lockAVPairs) {
    DEBUG2 aout("lockAll "<<*lockAVPairs);
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
            uint64_t encounterTime = getMinLockSnap(curr, readLogEntry);
            assert(encounterTime != 0x7fffffffffffffff);
            //assert(encounterTime);
            DEBUG2 aout("thread "<<Self->UniqID<<" trying to acquire lock "<<*curr->LockFor<<" with old-ver "<<(encounterTime>>1)<<")");

            // try to acquire locks
            // (and fail if their versions have changed since encounterTime)
            if (!curr->LockFor->tryAcquire(Self, encounterTime)) {
                DEBUG2 aout("thread "<<Self->UniqID<<" failed to acquire lock "<<*curr->LockFor);
                // if we fail to acquire a lock, we must
                // unlock all previous locks that we acquired
                for (AVPair* toUnlock = lockAVPairs->head; toUnlock != curr; toUnlock = toUnlock->Next) {
                    DEBUG2 aout("thread "<<Self->UniqID<<" releasing lock "<<*curr->LockFor<<" in lockAll (will be ver "<<((curr->LockFor->getSnapshot()>>1)+1)<<")");
                    toUnlock->LockFor->release(Self);
                }
                return false;
            }
            DEBUG2 aout("thread "<<Self->UniqID<<" acquired lock "<<*curr->LockFor);
        }
    }
    return true;
}

__INLINE__ bool tryAcquireWriteSet(Thread* Self) {
//    return lockAll(Self, &Self->wrSet->locks.list);
//    return lockAll<long>(Self, &Self->wrSet->l.addresses.list)
//        && lockAll<float>(Self, &Self->wrSet->f.addresses.list);
    return lockAll(Self, Self->wrSet);
}

// NOT TO BE INVOKED DIRECTLY
// releases all locks on addresses in a Log
/*__INLINE__*/ void releaseAll(Thread* Self, List* lockAVPairs) {
    DEBUG2 aout("releaseAll "<<*lockAVPairs);
    AVPair* const stop = lockAVPairs->put;
    for (AVPair* curr = lockAVPairs->head; curr != stop; curr = curr->Next) {
        DEBUG2 aout("thread "<<Self->UniqID<<" releasing lock "<<*curr->LockFor<<" in releaseAll (will be ver "<<((curr->LockFor->getSnapshot()>>1)+1)<<")");
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
    DEBUG2 aout("thread "<<Self->UniqID<<" validateLockVersions(rdSet) "<<*lockAVPairs);//<<" "<<debug(holdingLocks));
    assert(Self->isFallback);

    AVPair* const stop = lockAVPairs->put;
    for (AVPair* curr = lockAVPairs->head; curr != stop; curr = curr->Next) {
        vLock* lock = curr->LockFor;
        uint64_t locksnap = lock->getSnapshot();
        LWSYNC; // prevent any following reads from being reordered before getSnapshot()
        if (locksnap & 1) {
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
            uint64_t encounterTime = getMinLockSnap(curr, writeLogEntry);
            //assert(encounterTime);

            if ((locksnap >> 1) != (encounterTime>>1)) {
                // the address is locked, it its version number has changed
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















void TxClearRWSets(void* _Self) {
    Thread* Self = (Thread*) _Self;
    Self->wrSet->clear();
    Self->rdSet->clear();
}

int TxCommit(void* _Self) {
    Thread* Self = (Thread*) _Self;
    // software path
    if (Self->isFallback) {
        SOFTWARE_BARRIER; // prevent compiler reordering of speculative execution before isFallback check in htm (for power)

        // return immediately if txn is read-only
        if (Self->IsRO) {
            DEBUG2 aout("thread "<<Self->UniqID<<" commits read-only txn");
            assert(Self->wrSet->currsz == 0);
            ++Self->CommitsSW;
            goto success;
        }

        // lock all addresses in the write-set
        DEBUG2 aout("thread "<<Self->UniqID<<" invokes tryLockWriteSet "<<*Self->wrSet);
        if (!tryAcquireWriteSet(Self)) {
            TxAbort(Self); // abort if we fail to acquire some lock
            // (note: after lockAll we hold either all or no locks)
        }

        // validate reads
        if (!validateReadSet(Self)) {
            // if we fail validation, release all locks and abort
            DEBUG2 aout("thread "<<Self->UniqID<<" TxCommit failed validation -> release locks & abort");
            releaseWriteSet(Self);
            TxAbort(Self);
        }

        // perform the actual writes
        LWSYNC; // prevent writes from being done before any validation (on power)
        Self->wrSet->writeForward(Self);

        // release all locks
        DEBUG2 aout("thread "<<Self->UniqID<<" committed -> release locks");
        releaseWriteSet(Self);
        ++Self->CommitsSW;
        TM_COUNTER_INC(htmCommit[PATH_FALLBACK], Self->UniqID);

    // hardware path
    } else {
        XEND();
        ++Self->CommitsHW;
        TM_COUNTER_INC(htmCommit[PATH_FAST_HTM], Self->UniqID);
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
        DEBUG2 aout("thread "<<Self->UniqID<<" software abort...");
        ++Self->Retries;
        ++Self->AbortsSW;
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

// #ifdef TXNL_MEM_RECLAMATION
//         // "abort" speculative allocations and speculative frees
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

inline intptr_t TxLoad_stm(void* _Self, volatile intptr_t* addr) {
    Thread* Self = (Thread*) _Self;

    // check whether addr is in the write-set
    AVPair* av = Self->wrSet->find(addr);
    if (av) return av->value;//unpackValue(av);

    // addr is NOT in the write-set; abort if it is locked
    vLock* lock = PSLOCK(addr);
    uint64_t locksnap = lock->getSnapshot();
    if (locksnap & 1) {// && !lock->isOwnedBy(Self)) { // impossible for us to hold a lock on it.
        DEBUG2 aout("thread "<<Self->UniqID<<" TxRead saw lock "<<renamePointer(lock)<<" was held (not by us) -> aborting (retries="<<Self->Retries<<")");
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
        TxAbort(Self);
    }
    return val;
}

inline intptr_t TxLoad_htm(void* _Self, volatile intptr_t* addr) {
#ifdef USE_SUSPEND_RESUME
    XSUSPEND();
    // abort if addr is locked
    vLock* lock = PSLOCK(addr);
    uint64_t locksnap = lock->getSnapshot();
    if (locksnap & 1) {
        XRESUME();
        XABORT(1); // jumps to abort handler
    }
    XRESUME();
#else
    // abort if addr is locked
    vLock* lock = PSLOCK(addr);
    /**
     * accessing the lock below causes huge aborts, even with ONE thread!
     * it's not a segfault, since those are apparently not sandboxed on power
     * (unless we're erroneously executing htm code on the stm path).
     * it also doesn't seem to be a pathology with lock placement or
     * cache associativity, since the problem persists even when i replace
     * the simple PSLOCK with murmurhash3 to shuffle the addr -> lock mapping.
     */
    uint64_t locksnap = lock->getSnapshot();
    if (locksnap & 1) XABORT(1); // jumps to abort handler
#endif
    return *addr;
}

inline void TxStore_stm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    Thread* Self = (Thread*) _Self;

    // check whether addr is in the write-set
    AVPair* av = Self->wrSet->find(addr); // searching for this twice is somewhat inefficient sometimes... but shouldn't be a big deal if writes are somewhat rare
    if (!av) {
        // addr is NOT in the write-set; have we read it yet?
        // if not, we MUST, to satisfy Trevor's assumptions / constraints on usage...
        if (!Self->rdSet->find(addr)) {
            TxLoad_stm(_Self, addr);
        }
    }

    // abort if addr is locked (since we do not hold any locks)
    vLock* lock = PSLOCK(addr);
    uint64_t locksnap = lock->getSnapshot();
    if (locksnap & 1) { // note: impossible for lock to be owned by us, since we hold no locks.
        DEBUG2 aout("thread "<<Self->UniqID<<" TxStore saw lock "<<renamePointer(lock)<<" was held (not by us) -> aborting (retries="<<Self->Retries<<")");
        TxAbort(Self);
    }

    // add addr to the write-set
    Self->wrSet->insertReplace(Self, addr, value, lock, locksnap);
    Self->IsRO = false; // txn is not read-only
}

inline void TxStore_htm(void* _Self, volatile intptr_t* addr, intptr_t value) {
    // abort if addr is locked
    vLock* lock = PSLOCK(addr);
    uint64_t locksnap = lock->getSnapshot();
    if (locksnap & 1) XABORT(2);//TxAbort((Thread*) _Self); // we cannot hold the lock (since we hold none)

    // increment version number (to notify s/w txns of the change)
    // and write value to addr (order unimportant because of h/w)
    lock->htmIncrementVersion();
    *addr = value;
}









void TxOnce() {
    CTASSERT((_TABSZ & (_TABSZ - 1)) == 0); /* must be power of 2 */

//    initSighandler(); /**** DEBUG CODE ****/
    TM_CREATE_COUNTERS();
    printf("%s %s\n", TM_NAME, "system ready\n");
#ifdef STACK_SPACE_LOCKTAB
//    memset(LockTab, 0, _TABSZ*sizeof(vLock));
#else
    LockTab = (vLockSpace*) malloc(_TABSZ*sizeof(vLockSpace));
    memset(LockTab, 0, _TABSZ*sizeof(vLockSpace));
#endif
}

void TxClearCounters() {
    printf("Printing counters for %s and then clearing them in preparation for the real trial.\n", TM_NAME);
    TM_PRINT_COUNTERS();
    TM_CLEAR_COUNTERS();
    printf("Counters cleared.\n");
}

void TxShutdown() {
    if (!__tm_counters) return; // didn't actually invoke TxOnce, so destructor not needed

    printf("%s system shutdown:\n    HTM_ATTEMPT_THRESH=%d\n", TM_NAME, HTM_ATTEMPT_THRESH);
    TM_PRINT_COUNTERS();
    TM_DESTROY_COUNTERS();
#ifdef STACK_SPACE_LOCKTAB
#else
    free(LockTab);
#endif
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
