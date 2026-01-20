#pragma once

#include <ostream>

#define LOCKBIT 1
//class vLockSnapshot {
//public:
////private:
//    uint64_t lockstate;
//public:
//    __INLINE__ vLockSnapshot();
//    __INLINE__ vLockSnapshot(uint64_t _lockstate);
//    __INLINE__ bool isLocked() const;
//    __INLINE__ uint64_t version() const;
//    friend std::ostream& operator<<(std::ostream &out, const vLockSnapshot &obj);
//};

class vLock {
    volatile uint64_t lock; // (Version,LOCKBIT)
    volatile void* volatile owner; // invariant: NULL when lock is not held; non-NULL (and points to thread that owns lock) only when lock is held (but sometimes may be NULL when lock is held). guarantees that a thread can tell if IT holds the lock (but cannot necessarily tell who else holds the lock).
private:
    __INLINE__ vLock(uint64_t lockstate);
public:
    __INLINE__ vLock();
    __INLINE__ uint64_t getSnapshot() const;
//    __INLINE__ vLockSnapshot getSnapshot() const;
//    __INLINE__ bool tryAcquire(void* thread);
    __INLINE__ bool tryAcquire(void* thread, uint64_t oldval);
    __INLINE__ void release(void* thread);
    // can be invoked only by a hardware transaction
    __INLINE__ void htmIncrementVersion();
    __INLINE__ bool isOwnedBy(void* thread);

    friend std::ostream& operator<<(std::ostream &out, const vLock &obj);
};
