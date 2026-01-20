#include "vlock.h"

#define LOCKBIT 1

// __INLINE__ vLockSnapshot::vLockSnapshot() {}
// __INLINE__ vLockSnapshot::vLockSnapshot(uint64_t _lockstate) {
//     lockstate = _lockstate;
// }
// __INLINE__ bool vLockSnapshot::isLocked() const {
//     return lockstate & LOCKBIT;
// }
// __INLINE__ uint64_t vLockSnapshot::version() const {
//     cout<<"LOCKSTATE="<<lockstate<<" ~LOCKBIT="<<(~LOCKBIT)<<" VERSION="<<(lockstate & (~LOCKBIT))<<endl;
//     return lockstate & (~LOCKBIT);
// }
// friend std::ostream& operator<<(std::ostream &out, const vLockSnapshot &obj);

__INLINE__ vLock::vLock(uint64_t lockstate) {
    lock = lockstate;
    owner = 0;
}
__INLINE__ vLock::vLock() {
    lock = 0;
    owner = 0;
}
__INLINE__ uint64_t vLock::getSnapshot() const {
    return lock;
}
//    __INLINE__ vLockSnapshot vLockSnapshot::getSnapshot() const {
//        vLockSnapshot retval (lock);
////        __sync_synchronize();
//        return retval;
//    }
//    __INLINE__ bool vLockSnapshot::tryAcquire(void* thread) {
//        if (thread == owner) return true; // reentrant acquire
//        uint64_t val = lock & (~LOCKBIT);
//        bool retval = __sync_bool_compare_and_swap(&lock, val, val+1);
//        if (retval) {
//            owner = thread;
////            __sync_synchronize();
////            SOFTWARE_BARRIER;
//        }
////        __sync_synchronize();
//        return retval;
//    }
__INLINE__ bool vLock::tryAcquire(void* thread, uint64_t oldval) {
//    if (thread == owner) return true; // reentrant acquire
#if defined __PPC__ || defined __POWERPC__  || defined powerpc || defined _POWER || defined __ppc__ || defined __powerpc__
    if (oldval & 1) return false;
    while (1) {
        uint64_t v = (uint64_t) __ldarx(&lock);
        if (v != oldval) return false;
        if (__stdcx(&lock, oldval+1)) {
            LWSYNC; // prevent assignment of owner from being moved before lock acquisition (on power)
            owner = thread;
            SYNC_RMW; // prevent instructions in the critical section from being moved before lock acquisition or owner assignment (on power)
            return true;
        }
    }
#else
    bool retval = __sync_bool_compare_and_swap(&lock, oldval, oldval+1);
    if (retval) {
        owner = thread;
    }
    SOFTWARE_BARRIER;
    return retval;
#endif
}
__INLINE__ void vLock::release(void* thread) {
    // note: even without a membar here, the read of owner cannot be moved before our last lock acquisition (on power)
    if (thread == owner) { // reentrant release (assuming the lock should be released on the innermost release() call)
        owner = 0; // cannot be moved earlier by a processor without violating single-thread consistency
        SOFTWARE_BARRIER;
        LWSYNC; // prevent unlock from being moved before instructions in the critical section (on power)
        ++lock;
    }
}
// can be invoked only by a hardware transaction
__INLINE__ void vLock::htmIncrementVersion() {
    lock += 2;
}
__INLINE__ bool vLock::isOwnedBy(void* thread) {
    return (thread == owner);
}

//std::ostream& operator<<(std::ostream& out, const vLockSnapshot& obj) {
//    return out<<"ver="<<obj.version()
//                <<",locked="<<obj.isLocked()
//                ;//<<"> (raw="<<obj.lockstate<<")";
//}

std::ostream& operator<<(std::ostream& out, const vLock& obj) {
    return out<<"<"<<obj.getSnapshot()<<",owner="<<(obj.owner?((Thread_void*) obj.owner)->UniqID:-1)<<">@"<<renamePointer(&obj);
}
