/*
 * File:   dsbench_tm.h
 * Author: trbot
 *
 * Created on December 14, 2016, 4:01 PM
 */

#ifndef DSBENCH_TM_H
#define	DSBENCH_TM_H

#include <stdio.h>
#include <string.h>
#include <type_traits> // for is_pointer<T>
#include <execinfo.h>
// #include "death_handler.h"

#ifndef MAX_THREADS_POW2
    #define MAX_THREADS_POW2 128 // MUST BE A POWER OF TWO, since this is used for some bitwise operations
#endif
#ifndef SOFTWARE_BARRIER
    #define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

// avoiding false sharing
#ifndef PREFETCH_SIZE_WORDS
    #define PREFETCH_SIZE_WORDS 16
#endif
#ifndef PREFETCH_SIZE_BYTES
    #define PREFETCH_SIZE_BYTES 128
#endif
#ifndef CAT2
    #define CAT2(x, y) x##y
#endif
#ifndef CAT
    #define CAT(x, y) CAT2(x, y)
#endif
#ifndef PAD
    #define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#ifndef likely
    #define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
    #define unlikely(x)     __builtin_expect((x),0)
#endif

#define TM_ARG                          STM_SELF,
#define TM_ARG_ALONE                    STM_SELF
#define TM_ARGDECL                      STM_THREAD_T* TM_ARG
#define TM_ARGDECL_ALONE                STM_THREAD_T* TM_ARG_ALONE

#define TM_STARTUP()                    STM_STARTUP()
#define TM_SHUTDOWN()                   STM_SHUTDOWN()

#define TM_THREAD_ENTER(tid)            TM_ARG_ALONE = STM_NEW_THREAD((tid)); \
                                        STM_INIT_THREAD(TM_ARG_ALONE, tid)
#define TM_THREAD_EXIT()                STM_FREE_THREAD(TM_ARG_ALONE)

#define TM_MALLOC(size)                 STM_MALLOC(__tm_self, size)
#define TM_FREE(ptr)                    STM_FREE(__tm_self, ptr)

#ifdef NDEBUG
#   define TM_BEGIN()                   STM_BEGIN_WR(__tm_self); SOFTWARE_BARRIER;
#   define TM_BEGIN_RO()                STM_BEGIN_RD(__tm_self); SOFTWARE_BARRIER;
#   define TM_END()                     SOFTWARE_BARRIER; STM_END(__tm_self);
#else
#   define TM_BEGIN()                   STM_BEGIN_WR(__tm_self); __tm.tx_active = 1; SOFTWARE_BARRIER;
#   define TM_BEGIN_RO()                STM_BEGIN_RD(__tm_self); __tm.tx_active = 1; SOFTWARE_BARRIER;
#   define TM_END()                     SOFTWARE_BARRIER; STM_END(__tm_self); __tm.tx_active = 0;
#endif
#define TM_RESTART()                    STM_RESTART(__tm_self)

#define TM_SHARED_READ_P(var)           STM_READ_P(__tm_self, var)
#define TM_SHARED_WRITE_P(var, val)     STM_WRITE_P(__tm_self, (var), val)

#ifdef buffered_durable_tl2
#include "buffered_durable_tl2/stm.h"
#include "buffered_durable_tl2/tmalloc.h"
#include "buffered_durable_tl2/tmalloc.c"
#include "buffered_durable_tl2/tl2.h"
#include "buffered_durable_tl2/tl2.c"
// #warning Using tl2
#else
#error MUST DEFINE A PTM IMPLEMENTATION. ONE OF: buffered_durable_tl2 
#endif

PAD;
volatile int __initter_id_reservations[2*MAX_THREADS_POW2];
volatile int __tm_lock = 0;
volatile int __tm_global_done = 0;
PAD;

// extern __thread int tid;

template <typename DUMMY=int> // to prevent me from having to compile this code outside of the h-file...
class TMThreadContext {
  public:
  PAD;
    TM_ARGDECL_ALONE;
    int initter_id;
    bool tx_active;    
  PAD;

    TMThreadContext() {
        tx_active = 0;

        // ONE thread does global init
        // printf("TMThreadContext: waiting on lock\n");
        while (__tm_lock || !__sync_bool_compare_and_swap(&__tm_lock, 0, 1)) {}
        // printf("TMThreadContext: got lock\n");

        // lock acquired
        if (!__tm_global_done) {
            // printf("TMThreadContext: global initialization\n");
            __tm_global_done = 1;
            for (int i=0;i<2*MAX_THREADS_POW2;++i) {
                __initter_id_reservations[i] = 0;
            }
            TM_STARTUP();
            __sync_synchronize();
        }
        __tm_lock = 0;
        __sync_synchronize();

        // EACH thread does local init
        initter_id = -1;
        for (int i=0;i<2*MAX_THREADS_POW2;++i) {
            if (__sync_bool_compare_and_swap(&__initter_id_reservations[i], 0, 1)) {
                initter_id = i;
                break;
            }
        }
        if (initter_id < 0) {
            printf("ERROR: initter_id not set. could be that MAX_THREADS_POW2 needs to be increased.\n");
            exit(-1);
        }
        // printf("TMThreadContext: thread initialization initter_id=%d\n", initter_id);
        TM_THREAD_ENTER(initter_id); // SETS TM_ARGDECL_ALONE !!!
        // printf("TMThreadContext: entered initter_id=%d\n", initter_id);
    }
    ~TMThreadContext() {
        // printf("~TMThreadContext: destructor for initter_id=%d\n", initter_id);
        // __sync_synchronize();
        __initter_id_reservations[initter_id] = 0;
        __sync_synchronize();
        TM_THREAD_EXIT();
    }
};
thread_local TMThreadContext<> __tm;
thread_local int const __tm_id = __tm.initter_id;
thread_local void * const __tm_self = __tm.TM_ARG_ALONE;
PAD;

void __attribute__((destructor)) __tm_global_destructor() {
    TM_SHUTDOWN();
}

enum tx_safety {
    SAFE,
    UNSAFE
};


template <tx_safety mode, typename T>
class tx_field {
  private:
    uintptr_t volatile bits;

  public:
    __attribute__((always_inline)) tx_field() {
        if constexpr (sizeof(T) > sizeof(uintptr_t)) {
            fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
            // Debug::DeathHandler dh; dh.set_append_pid(true);
            abort();
        }
    }

    __attribute__((always_inline)) inline T load_unsafe() {
        SOFTWARE_BARRIER;
        return (T) bits;
    }
    __attribute__((always_inline)) inline T load() {
        if constexpr (mode == UNSAFE) {
#if !defined NDEBUG
            // printf("__tm thread %3d: unsafe load %p\n", __tm.initter_id, &bits);
            if (__tm.tx_active) {
                fprintf(stderr, "\nFATAL ERROR: non-transactional loads must be performed outside of any active transactions\n\n");
                // Debug::DeathHandler dh; dh.set_append_pid(true);
                abort();
            }
#endif
            T retval = load_unsafe();
            return retval;
        } else {
#if !defined NDEBUG
            SOFTWARE_BARRIER;
            // printf("__tm thread %3d: safe load %p\n", __tm.initter_id, &bits);
            if (!__tm.tx_active) {
                fprintf(stderr, "\nFATAL ERROR: transactional loads must be performed in an active transaction\n\n");
                // Debug::DeathHandler dh; dh.set_append_pid(true);
                abort();
            }
#endif
            // TM_ARGDECL_ALONE = __tm_self;
            T retval = (T) (uintptr_t) TM_SHARED_READ_P(bits);
            return retval;
        }
    }
    __attribute__((always_inline)) inline operator T() {
        return load();
    }
    __attribute__((always_inline)) inline T operator->() {
        assert(std::is_pointer<T>::value);
        return *this;
    }

    __attribute__((always_inline)) inline T store_unsafe(T const other) {
        SOFTWARE_BARRIER;
        bits = (uintptr_t) other;
        return other;
    }
    __attribute__((always_inline)) inline T store(T const other) {
        if constexpr (mode == UNSAFE) {
#if !defined NDEBUG
            // printf("__tm thread %3d: unsafe store %p\n", __tm.initter_id, &bits);
            if (__tm.tx_active) {
                fprintf(stderr, "\nFATAL ERROR: non-transactional stores must be performed outside of any active transactions\n\n");
                // Debug::DeathHandler dh; dh.set_append_pid(true);
                abort();
            }
#endif
            return store_unsafe(other);
        } else {
#if !defined NDEBUG
            SOFTWARE_BARRIER;
            // printf("__tm thread %3d: safe store %p\n", __tm.initter_id, &bits);
            if (!__tm.tx_active) {
                fprintf(stderr, "\nFATAL ERROR: transactional stores must be performed in an active transaction\n\n");
                // Debug::DeathHandler dh; dh.set_append_pid(true);
                abort();
            }
#endif
            // TM_ARGDECL_ALONE = __tm_self;
            TM_SHARED_WRITE_P(bits, (void *) (uintptr_t) other);
            return (T) (uintptr_t) other;
        }
    }
    __attribute__((always_inline)) inline T operator=(T const other) {
        return store(other);
    }
    // inline tx_field<mode, T> operator=(tx_field<mode, T> const & other) = delete;
    inline tx_field<mode, T> operator=(tx_field<mode, T> const & other) {
        fprintf(stderr, "\nFATAL ERROR: your code tries to assign a value of type tx_field<mode,T> to a field of type tx_field<mode,T>. this is a mistake. you are likely trying to read one transactional address and assign it to another transactional address in the same line. you can split these reads/writes into separate lines, or call 'field.load()' explicitly on the field you're reading before assigning to another address in the same line. this situation arises because the compiler cannot disambiguate between (1) a copy assignment of one tx_field to another, i.e., an = operator with tx_field as its argument, and (2) a read of a tx_field<mode,T> to produce a T, followed by an = operator with argument T. to see WHERE in your code this is happening, check out the helpful backtrace below... (if there is no backtrace, compile with use_asserts=1 to get a nice colorful one.) WARNING: because of inlining, a stack trace may not take you right to your faulty code... in this case, try compiling with make -j no_optimize=1\n\n");
        // Debug::DeathHandler dh; dh.set_append_pid(true);
        abort();
    }
};

#ifndef CTASSERT
    #define CTASSERT(x)                 ({ int a[1-(2*!(x))] __attribute((unused)); a[0] = 0;})
#endif

#endif	/* DSBENCH_TM_H */
