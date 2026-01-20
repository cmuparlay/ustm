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

#if defined hytm1
#include "hytm1/stm.h"
#include "hytm1/hytm1.h"
#include "hytm1/hytm1.cpp"
// #warning Using hytm1
#elif defined hytm2
#include "hytm2/stm.h"
#include "hytm2/hytm2.h"
#include "hytm2/hytm2.cpp"
// #warning Using hytm2
#elif defined hytm2_3path
#include "hytm2_3path/stm.h"
#include "hytm2_3path/hytm2_3path.h"
#include "hytm2_3path/hytm2_3path.cpp"
// #warning Using hytm2_3path
#elif defined hytm3
#include "hytm3/stm.h"
#include "hytm3/hytm3.h"
#include "hytm3/hytm3.cpp"
// #warning Using hytm3
#elif defined hytm4
#include "hytm4/stm.h"
#include "hytm4/hytm4.h"
#include "hytm4/hytm4.cpp"
// #warning Using hytm4
#elif defined hytm5
#include "hytm5/stm.h"
#include "hytm5/hytm5.h"
#include "hytm5/hytm5.cpp"
// #warning Using hytm5
#elif defined hybridnorec
#include "hybridnorec/stm.h"
#include "hybridnorec/hybridnorec.h"
#include "hybridnorec/hybridnorec.cpp"
// #warning Using hybridnorec
#elif defined tl2
#include "tl2/stm.h"
#include "tl2/tmalloc.h"
#include "tl2/tmalloc.c"
#include "tl2/tl2.h"
#include "tl2/tl2.c"
// #warning Using tl2
#elif defined norec
#include "norec/stm.h"
#include "norec/norec.h"
#include "norec/norec.cpp"
// #warning Using norec
#elif defined rhnorec_post
#include "rhnorec_post/stm.h"
#include "rhnorec_post/rhnorec_post.h"
#include "rhnorec_post/rhnorec_post.cpp"
// #warning Using rhnorec_post
#elif defined rhnorec_post_pre
#include "rhnorec_post_pre/stm.h"
#include "rhnorec_post_pre/rhnorec_post_pre.h"
#include "rhnorec_post_pre/rhnorec_post_pre.cpp"
// #warning Using rhnorec_post_pre
#elif defined tl3
#include "tl3/stm.h"
#include "tl3/tl3.h"
#include "tl3/avpair.cpp"
#include "tl3/vlock.cpp"
#include "tl3/thread.cpp"
#include "tl3/log.cpp"
#include "tl3/tl3.cpp"
#else
#error MUST DEFINE A TM IMPLEMENTATION. ONE OF: hytm1 hytm2 hytm2_3path hytm3 hytm4 hybridnorec tl2 norec rhnorec_post rhnorec_post_pre
#endif

PAD;
volatile int __initter_id_reservations[2*MAX_THREADS_POW2];
volatile int __tm_lock = 0;
volatile int __tm_global_done = 0;
PAD;

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

// template <typename T>
// class tx_field {
//   private:
//     uintptr_t volatile bits;
//
//   public:
//     tx_field() {
//         if (sizeof(T) > sizeof(uintptr_t)) {
//             fprintf(stderr, "FATAL ERROR: tx_field has type T larger than uintptr_t\n");
//             exit(1);
//         }
//     }
//
//   private:
//     template <typename CAST_TYPE>
//     inline T load_intermediate_cast_p() {
//         TM_ARGDECL_ALONE = __tm_self;
//         return (T) (CAST_TYPE) TM_SHARED_READ_P(bits);
//     }
//     template <typename CAST_TYPE>
//     inline T load_intermediate_cast_l() {
//         TM_ARGDECL_ALONE = __tm_self;
//         return (T) (CAST_TYPE) TM_SHARED_READ_L(bits);
//     }
//
//   public:
//     inline T load_unsafe() {
//         return (T) bits;
//     }
//     inline T load() {
//         if (unlikely(!__tm.tx_active)) return load_unsafe();
//         if (std::is_pointer<T>::value) {
//             return load_intermediate_cast_p<uintptr_t>();
//         } else {
//             return load_intermediate_cast_l<size_t>();
//         }
//     }
//     inline operator T() {
//         return load();
//     }
//     inline T operator->() {
//         assert(std::is_pointer<T>::value);
//         return *this;
//     }
//
//   private:
//     template <typename CAST_TYPE>
//     inline T store_intermediate_cast_p(const T& other) {
//         TM_ARGDECL_ALONE = __tm_self;
//         TM_SHARED_WRITE_P(bits, (void *) (CAST_TYPE) other);
//         return (T) (CAST_TYPE) other;
//     }
//     template <typename CAST_TYPE>
//     inline T store_intermediate_cast_l(const T& other) {
//         TM_ARGDECL_ALONE = __tm_self;
//         TM_SHARED_WRITE_L(bits, (CAST_TYPE) other);
//         return (T) (CAST_TYPE) other;
//     }
//
//   public:
//     inline T store_unsafe(const T &other) {
//         bits = (uintptr_t) other;
//         return other;
//     }
//     inline T store(const T& other) {
//         if (unlikely(!__tm.tx_active)) return store_unsafe(other);
//         if (std::is_pointer<T>::value) {
//             return store_intermediate_cast_p<uintptr_t>(other);
//         } else {
//             return store_intermediate_cast_l<size_t>(other);
//         }
//     }
//     inline T operator=(const T &other) {
//         return store(other);
//     }
// };

enum tx_safety {
    SAFE,
    UNSAFE
};

// template <tx_safety mode, typename T>
// class tx_field {
//   private:
//     uintptr_t volatile bits;

//   public:
//     tx_field() {
//         if constexpr (sizeof(T) > sizeof(uintptr_t)) {
//             fprintf(stderr, "FATAL ERROR: tx_field has type T larger than uintptr_t\n");
//             exit(1);
//         }
//     }

//     inline T load_unsafe() {
//         // printf("__tm thread %3d: unsafe load %p\n", __tm.initter_id, &bits);
//         assert(!__tm.tx_active && "non-transactional loads must be performed outside of any active transactions");
//         return (T) bits;
//     }
//     inline T load() {
//         if constexpr (mode == UNSAFE) return load_unsafe();
//         // printf("__tm thread %3d: safe load %p\n", __tm.initter_id, &bits);
//         assert(__tm.tx_active && "transactional loads must be performed in an active transaction");
//         TM_ARGDECL_ALONE = __tm_self;
//         return (T) (size_t) TM_SHARED_READ_L(bits);
//     }
//     inline operator T() {
//         return load();
//     }
//     inline T operator->() {
//         assert(std::is_pointer<T>::value);
//         return *this;
//     }

//     inline T store_unsafe(const T &other) {
//         printf("__tm thread %3d: unsafe store %p\n", __tm.initter_id, &bits);
//         assert(__tm.tx_active && "non-transactional stores must be performed outside of any active transactions");
//         bits = (uintptr_t) other;
//         return other;
//     }
//     inline T store(const T& other) {
//         if constexpr (mode == UNSAFE) return store_unsafe(other);
//         printf("__tm thread %3d: safe store %p\n", __tm.initter_id, &bits);
//         assert(__tm.tx_active && "transactional stores must be performed in an active transaction");
//         TM_ARGDECL_ALONE = __tm_self;
//         TM_SHARED_WRITE_L(bits, (size_t) other);
//         return (T) (size_t) other;
//     }
//     inline T operator=(const T& other) {
//         return store(other);
//     }
// };

// template <tx_safety mode, typename T>
// class tx_field<mode, T*> {
//   private:
//     uintptr_t volatile bits;

//   public:
//     tx_field() {
//         if constexpr (sizeof(T*) > sizeof(uintptr_t)) {
//             fprintf(stderr, "FATAL ERROR: tx_field has type T* larger than uintptr_t\n");
//             exit(1);
//         }
//     }

//     inline T* load_unsafe() {
//         // printf("__tm thread %3d: unsafe load %p\n", __tm.initter_id, &bits);
//         assert(!__tm.tx_active && "non-transactional loads must be performed outside of any active transactions");
//         return (T*) bits;
//     }
//     inline T* load() {
//         if constexpr (mode == UNSAFE) return load_unsafe();
//         // printf("__tm thread %3d: safe load %p\n", __tm.initter_id, &bits);
//         assert(__tm.tx_active && "transactional loads must be performed in an active transaction");
//         TM_ARGDECL_ALONE = __tm_self;
//         return (T*) (uintptr_t) TM_SHARED_READ_P(bits);
//     }
//     inline operator T*() {
//         return load();
//     }
//     inline T* operator->() {
//         assert(std::is_pointer<T*>::value);
//         return *this;
//     }

//     inline T* store_unsafe(T* const other) {
//         printf("__tm thread %3d: unsafe store %p\n", __tm.initter_id, &bits);
//         assert(!__tm.tx_active && "non-transactional stores must be performed outside of any active transactions");
//         bits = (uintptr_t) other;
//         return other;
//     }
//     inline T* store(T* const other) {
//         if constexpr (mode == UNSAFE) return store_unsafe(other);
//         printf("__tm thread %3d: safe store %p\n", __tm.initter_id, &bits);
//         assert(__tm.tx_active && "transactional stores must be performed in an active transaction");
//         TM_ARGDECL_ALONE = __tm_self;
//         TM_SHARED_WRITE_P(bits, (void *) other);
//         return (T*) (uintptr_t) other;
//     }
//     inline T* operator=(T* const other) {
//         return store(other);
//     }
// };

/**
 * code is a hybrid of:
 * linux manpages: https://man7.org/linux/man-pages/man3/backtrace.3.html
 * and stackoverflow: https://stackoverflow.com/questions/15129089/is-there-a-way-to-dump-stack-trace-with-line-number-from-a-linux-release-binary
 * and this site: https://www.thetopsites.net/article/51890324.shtml
 */
// #define BT_BUF_SIZE 100
// void error_show_backtrace() {
//     // int j;
//     int nptrs;
//     void *buffer[BT_BUF_SIZE];
//     char **strings;
//     nptrs = backtrace(buffer, BT_BUF_SIZE);
//     fprintf(stderr, "\nINFO: here's a helpful backtrace() for you.\n");
//     strings = backtrace_symbols(buffer, nptrs);
//     if (strings == NULL) { perror("backtrace_symbols"); exit(-1); }

//     char exe[1024];
//     int ret = readlink("/proc/self/exe",exe,sizeof(exe)-1);
//     if (ret == -1) {
//         fprintf(stderr,"FATAL ERROR: getting /proc/self/exe. cannot produce stack trace. you'll have to do that yourself.\n");
//         exit(1);
//     }
//     exe[ret] = 0;

//     /* skip first stack frame (points here) */
//     for (int i=1; i<nptrs; ++i) {
//         printf("[bt] #%d %s\n", i, strings[i]);
//         char syscom[1024];
//         sprintf(syscom, "addr2line %p -e %s", buffer[i], exe); //last parameter is the name of this app
//         // fprintf(stderr, "RUNNING %s\n", syscom);
//         dup2(2,1); // redirect output to stderr
//         if (system(syscom) < 0) {
//             fprintf(stderr, "FATAL ERROR: running addr2line. cannot produce stack trace. you'll have to do that yourself.\n");
//             exit(-1);
//         }
//     }
//     free(strings);
// }

// // code stolen from: https://www.thetopsites.net/article/51890324.shtml
// #include <stdio.h>
// #include <stdlib.h>
// #include <sys/wait.h>
// #include <unistd.h>
// void error_show_backtrace() {
//     char pid_buf[30];
//     sprintf(pid_buf, "%d", getpid());

//     char exe[1024];
//     int ret = readlink("/proc/self/exe", exe, sizeof(exe)-1);
//     if (ret == -1) {
//         fprintf(stderr,"FATAL ERROR: getting /proc/self/exe. cannot produce stack trace. you'll have to do that yourself.\n");
//         exit(1);
//     }
//     exe[ret] = 0;

//     int child_pid = fork();
//     if (!child_pid) {
//         dup2(2,1); // redirect output to stderr
//         fprintf(stdout,"stack trace for %s pid=%s\n", exe, pid_buf);
//         execlp("gdb", "gdb", "--batch", "-n", "-ex", "thread", "-ex", "bt", exe, pid_buf, NULL);
//         abort(); /* If gdb failed to start */
//     } else {
//         waitpid(child_pid,NULL,0);
//     }
// }

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
