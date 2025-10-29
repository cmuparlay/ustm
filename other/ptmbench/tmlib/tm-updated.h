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
#   define TM_BEGIN()                   STM_BEGIN_WR(__tm_self); __tm.tx_active = 1; SOFTWARE_BARRIER;
#   define TM_BEGIN_RO()                STM_BEGIN_RD(__tm_self); __tm.tx_active = 1; SOFTWARE_BARRIER;
#   define TM_END()                     SOFTWARE_BARRIER; STM_END(__tm_self);  __tm.tx_active = 0;
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

// template <tx_safety mode, typename T>
// class tx_field {
//   private:
//     uintptr_t volatile bits;

//   public:
//     __attribute__((always_inline)) tx_field() {
//         if constexpr (sizeof(T) > sizeof(uintptr_t)) {
//             fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
//             // Debug::DeathHandler dh; dh.set_append_pid(true);
//             abort();
//         }
//     }

//     __attribute__((always_inline)) inline T load_unsafe() {
//         SOFTWARE_BARRIER;
//         return (T) bits;
//     }
//     __attribute__((always_inline)) inline T load() {
//         if constexpr (mode == UNSAFE) {
// #if !defined NDEBUG
//             // printf("__tm thread %3d: unsafe load %p\n", __tm.initter_id, &bits);
//             if (__tm.tx_active) {
//                 fprintf(stderr, "\nFATAL ERROR: non-transactional loads must be performed outside of any active transactions\n\n");
//                 // Debug::DeathHandler dh; dh.set_append_pid(true);
//                 abort();
//             }
// #endif
//             T retval = load_unsafe();
//             return retval;
//         } else {
// #if !defined NDEBUG
//             SOFTWARE_BARRIER;
//             // printf("__tm thread %3d: safe load %p\n", __tm.initter_id, &bits);
//             if (!__tm.tx_active) {
//                 fprintf(stderr, "\nFATAL ERROR: transactional loads must be performed in an active transaction\n\n");
//                 // Debug::DeathHandler dh; dh.set_append_pid(true);
//                 abort();
//             }
// #endif
//             // TM_ARGDECL_ALONE = __tm_self;
//             T retval = (T) (uintptr_t) TM_SHARED_READ_P(bits);
//             return retval;
//         }
//     }
//     __attribute__((always_inline)) inline operator T() {
//         return load();
//     }
//     __attribute__((always_inline)) inline T operator->() {
//         assert(std::is_pointer<T>::value);
//         return *this;
//     }

//     __attribute__((always_inline)) inline T store_unsafe(T const other) {
//         SOFTWARE_BARRIER;
//         bits = (uintptr_t) other;
//         return other;
//     }
//     __attribute__((always_inline)) inline T store(T const other) {
//         if constexpr (mode == UNSAFE) {
// #if !defined NDEBUG
//             // printf("__tm thread %3d: unsafe store %p\n", __tm.initter_id, &bits);
//             if (__tm.tx_active) {
//                 fprintf(stderr, "\nFATAL ERROR: non-transactional stores must be performed outside of any active transactions\n\n");
//                 // Debug::DeathHandler dh; dh.set_append_pid(true);
//                 abort();
//             }
// #endif
//             return store_unsafe(other);
//         } else {
// #if !defined NDEBUG
//             SOFTWARE_BARRIER;
//             // printf("__tm thread %3d: safe store %p\n", __tm.initter_id, &bits);
//             if (!__tm.tx_active) {
//                 fprintf(stderr, "\nFATAL ERROR: transactional stores must be performed in an active transaction\n\n");
//                 // Debug::DeathHandler dh; dh.set_append_pid(true);
//                 abort();
//             }
// #endif
//             // TM_ARGDECL_ALONE = __tm_self;
//             TM_SHARED_WRITE_P(bits, (void *) (uintptr_t) other);
//             return (T) (uintptr_t) other;
//         }
//     }
//     __attribute__((always_inline)) inline T operator=(T const other) {
//         return store(other);
//     }
//     // inline tx_field<mode, T> operator=(tx_field<mode, T> const & other) = delete;
//     inline tx_field<mode, T> operator=(tx_field<mode, T> const & other) {
//         fprintf(stderr, "\nFATAL ERROR: your code tries to assign a value of type tx_field<mode,T> to a field of type tx_field<mode,T>. this is a mistake. you are likely trying to read one transactional address and assign it to another transactional address in the same line. you can split these reads/writes into separate lines, or call 'field.load()' explicitly on the field you're reading before assigning to another address in the same line. this situation arises because the compiler cannot disambiguate between (1) a copy assignment of one tx_field to another, i.e., an = operator with tx_field as its argument, and (2) a read of a tx_field<mode,T> to produce a T, followed by an = operator with argument T. to see WHERE in your code this is happening, check out the helpful backtrace below... (if there is no backtrace, compile with use_asserts=1 to get a nice colorful one.) WARNING: because of inlining, a stack trace may not take you right to your faulty code... in this case, try compiling with make -j no_optimize=1\n\n");
//         // Debug::DeathHandler dh; dh.set_append_pid(true);
//         abort();
//     }
// };


struct tx_tuple {
    uintptr_t volatile bits;
    // uintptr_t volatile old;
    // uintptr_t volatile pversionNum;
    // uintptr_t volatile txId;
    // PAD;
};

template <typename T>
struct tx_field {
  private:
    tx_tuple data;

  public:
    tx_field() {
        if constexpr (sizeof(T) > sizeof(uintptr_t)) {
            fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
            abort();
        }
    }

    tx_field(T initVal) { 
        if constexpr (sizeof(T) > sizeof(uintptr_t)) {
            fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
            abort();
        }

        pstore(initVal); 
    }

    inline void pstore(T other) {        
#if !defined NDEBUG
        SOFTWARE_BARRIER;
        if (!__tm.tx_active) {
            fprintf(stderr, "\nFATAL ERROR: transactional stores must be performed in an active transaction\n\n");
            abort();
        }
#endif
        TM_SHARED_WRITE_P(data.bits, (void *) (uintptr_t) other);
        // return (T) (uintptr_t) other;        
        return;        
    }

    inline T pload() {
#if !defined NDEBUG
        SOFTWARE_BARRIER;
        if (!__tm.tx_active) {
            fprintf(stderr, "\nFATAL ERROR: transactional loads must be performed in an active transaction\n\n");
            abort();
        }
#endif
        T retval = (T) (uintptr_t) TM_SHARED_READ_P(data.bits);
        return retval;
    } 

    inline T pload() const {
#if !defined NDEBUG
        SOFTWARE_BARRIER;
        if (!__tm.tx_active) {
            fprintf(stderr, "\nFATAL ERROR: transactional loads must be performed in an active transaction\n\n");
            abort();
        }
#endif
        T retval = (T) (uintptr_t) TM_SHARED_READ_P(data.bits);
        return retval;
    } 

    // Casting operator
    operator T() { return pload(); }
    // Casting to const
    operator T() const { return pload(); }

    // Prefix increment operator: ++x
    void operator++ () { pstore(pload()+1); }
    // Prefix decrement operator: --x
    void operator-- () { pstore(pload()-1); }
    void operator++ (int) { pstore(pload()+1); }
    void operator-- (int) { pstore(pload()-1); }
    tx_field<T>& operator+= (const T& rhs) { pstore(pload() + rhs); return *this; }
    tx_field<T>& operator-= (const T& rhs) { pstore(pload() - rhs); return *this; }

    // Equals operator
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator == (const tx_field<Y> &rhs) { return pload() == rhs; }
    // Difference operator: first downcast to T and then compare
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator != (const tx_field<Y> &rhs) { return pload() != rhs; }
    // Relational operators
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator < (const tx_field<Y> &rhs) { return pload() < rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator > (const tx_field<Y> &rhs) { return pload() > rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator <= (const tx_field<Y> &rhs) { return pload() <= rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator >= (const tx_field<Y> &rhs) { return pload() >= rhs; }

       // Equals operator
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator == (tx_field<Y> &rhs) { return pload() == rhs; }
    // Difference operator: first downcast to T and then compare
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator != (tx_field<Y> &rhs) { return pload() != rhs; }
    // Relational operators
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator < (tx_field<Y> &rhs) { return pload() < rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator > (tx_field<Y> &rhs) { return pload() > rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator <= (tx_field<Y> &rhs) { return pload() <= rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator >= (tx_field<Y> &rhs) { return pload() >= rhs; }

    // Operator arrow ->
    T operator->() { return pload(); }

    // Copy constructor
    tx_field<T>(const tx_field<T>& other) { pstore(other.pload()); }

    // Assignment operator from a tx_field<T>
    tx_field<T>& operator=(tx_field<T>& other) {
        pstore(other.pload());
        return *this;
    }

    tx_field<T>& operator=(const tx_field<T>& other) {
        pstore(other.pload());
        return *this;
    }

    // Assignment operator from a value
    tx_field<T>& operator=(T value) {
        pstore(value);
        return *this;
    }

    // Operator &
    T* operator&() {
        return (T*)this;
    }
};

#define REGISTRY_MAX_THREADS 218

// extern __thread int tid;

template <template <typename> class P>
class EsLoco2 {
private:
    const bool debugOn = false;
    // Number of blocks in the freelists array.
    // Each entry corresponds to an exponent of the block size: 2^4, 2^5, 2^6... 2^40
    static const int kMaxBlockSize = 50; // 1024 TB of memory should be enough
    // Size after which we trigger a cleanup of the thread-local list/pool
    static const uint64_t kMaxListSize = 64;
    // Size of a thread-local slab (in bytes). When it runs out, we take another slab from poolTop.
    // Larger allocations go directly on poolTop which means doing a lot of large allocations from
    // multiple threads will result in contention.
    static const uint64_t kSlabSize = 4*1024*1024;

    volatile bool initialized = false;

    struct block {
        P<block*>   next;    // Pointer to next block in free-list (when block is in free-list)
        P<uint64_t> size2;   // Exponent of power of two of the size of this block in bytes.
    };

    // A persistent free-list of blocks
    struct FList {
        P<uint64_t> count;   // Number of blocks in the stack
        P<block*>   head;    // Head of the stack (where we push and pop)
        P<block*>   tail;    // Tail of the stack (used for faster transfers to/from gflist)

        // Push a block into this free list, incrementing the volatile
        // counter and adjusting the tail if needed. Does two pstores min.
        void pushBlock(block* myblock) {
            block* lhead = head;
            if (lhead == nullptr) {
                assert(count == 0);
                tail = myblock;                    // pstore()
            }
            myblock->next = lhead;                 // pstore()
            head = myblock;                        // pstore()
            count++;
        }

        // Pops a block from a given (persistent) free list, decrementing the
        // volatile counter and setting the tail to nullptr if list becomes empty.
        // Does 2 pstores.
        block* popBlock(void) {
            assert(count != 0);
            assert(tail != nullptr);
            count--;
            block* myblock = head;
            head = myblock->next;                // pstore()
            if (head == nullptr) tail = nullptr; // pstore()
            return myblock;
        }

        bool isFull(void) { return (count >= kMaxListSize); }
        bool isEmpty() { return (count == 0); }

        // Transfer this list to another free-list (oflist).
        // We use this method to transfer from the thread-local list to the global list.
        void transfer(FList* oflist) {
            assert(count != 0);
            assert(tail != nullptr);
            if (oflist->tail == nullptr) {
                assert (oflist->head == nullptr);
                oflist->tail = tail;                           // pstore()
            }
            tail->next = oflist->head;                         // pstore()
            oflist->head = head;                               // pstore()
            oflist->count += count;                            // pstore()
            count = 0;                                         // pstore()
            tail = nullptr;                                    // pstore()
            head = nullptr;                                    // pstore()
        }
    };

    // To avoid false sharing and lock conflicts, we put each thread-local data in one of these.
    // This is persistent (in PM).
    struct TLData {
        uint64_t     padding[8];
        // Thread-local array of persistent heads of free-list
        FList        tflists[kMaxBlockSize];
        // Thread-local slab pointer
        P<uint8_t*>  slabtop;
        // Thread-loal slab size
        P<uint64_t>  slabsize;
        TLData() {
            for (int i = 0; i < kMaxBlockSize; i++) {
                tflists[i].count = 0;             // pstore()
                tflists[i].head = nullptr;        // pstore()
                tflists[i].tail = nullptr;        // pstore()
            }
            slabtop = nullptr;                    // pstore()
            slabsize = 0;                         // pstore()
        }
    };

    // Complete metadata. This is PM
    struct ELMetadata {
        P<uint8_t*> top;
        FList       gflists[kMaxBlockSize];
        TLData      tldata[REGISTRY_MAX_THREADS];
        // This constructor should be called from within a transaction
        ELMetadata() {
            // The 'top' starts after the metadata header
            top = ((uint8_t*)this) + sizeof(ELMetadata);         // pstore()
            for (int i = 0; i < kMaxBlockSize; i++) {
                gflists[i].count = 0;             // pstore()
                gflists[i].head = nullptr;        // pstore()
                gflists[i].tail = nullptr;        // pstore()
            }
            for (int it = 0; it < REGISTRY_MAX_THREADS; it++) {
                new (&tldata[it]) TLData();                      // pstores()
            }
        }
    };

    // Volatile data
    uint8_t* poolAddr {nullptr};
    uint64_t poolSize {0};

    // Pointer to location of PM metadata of EsLoco
    ELMetadata* meta {nullptr};

    // For powers of 2, returns the highest bit, otherwise, returns the next highest bit
    uint64_t highestBit(uint64_t val) {
        uint64_t b = 0;
        while ((val >> (b+1)) != 0) b++;
        if (val > (1ULL << b)) return b+1;
        return b;
    }

    uint8_t* aligned(uint8_t* addr) {
        return (uint8_t*)((size_t)addr & (~0x3FULL)) + 128;
    }

public:
    void init(void* addressOfMemoryPool, size_t sizeOfMemoryPool, bool clearPool=true) {
        // Align the base address of the memory pool
        poolAddr = aligned((uint8_t*)addressOfMemoryPool);
        poolSize = sizeOfMemoryPool + (uint8_t*)addressOfMemoryPool - poolAddr;
        // The first thing in the pool is the ELMetadata
        meta = (ELMetadata*)poolAddr;
        if (clearPool) new (meta) ELMetadata();
        printf("\n\nINIT: EsLoco2 with poolAddr=%p and poolSize=%ld, up to %p\n\n", poolAddr, poolSize, poolAddr+poolSize);
    }

    // Resets the metadata of the allocator back to its defaults
    void reset() {
        std::memset(poolAddr, 0, sizeof(block)*kMaxBlockSize*(REGISTRY_MAX_THREADS+1));
        meta->top.pstore(nullptr);
    }

    // Returns the number of bytes that may (or may not) have allocated objects, from the base address to the top address
    uint64_t getUsedSize() {
        return meta->top.pload() - poolAddr;
    }

    // Takes the desired size of the object in bytes.
    // Returns pointer to memory in pool, or nullptr.
    // Does on average 1 store to persistent memory when re-utilizing blocks.
    void* malloc(size_t size) {
        // const int tid = ThreadRegistry::getTID();        
        const int tid = ((Thread_void*)(__tm_self))->UniqID;        
        // Adjust size to nearest (highest) power of 2
        uint64_t bsize = highestBit(size + sizeof(block));
        uint64_t asize = (1ULL << bsize);
        if (debugOn) printf("malloc(%ld) requested by thread %d,  block size exponent = %ld\n", size, tid, bsize);
        block* myblock = nullptr;
        // Check if there is a block of that size in the corresponding freelist
        if (!meta->tldata[tid].tflists[bsize].isEmpty()) {
            if (debugOn) printf("Found available block in thread-local freelist\n");
            myblock = meta->tldata[tid].tflists[bsize].popBlock();
        } else if (asize < kSlabSize/2) {
            // Allocations larger than kSlabSize/2 go on the global top
            if (asize >= meta->tldata[tid].slabsize.pload()) {
                if (debugOn) printf("Not enough space on current slab of thread %d for %ld bytes. Taking new slab from top\n",tid, asize);
                // Not enough space for allocation. Take a new slab from the global top and
                // discard the old one. This means fragmentation/leak is at most kSlabSize/2.
                if (meta->top.pload() + kSlabSize > poolSize + poolAddr) {
                    printf("EsLoco2: Out of memory for slab %ld for %ld bytes allocation\n", kSlabSize, size);
                    return nullptr;
                }
                meta->tldata[tid].slabtop = meta->top.pload();   // pstore()
                meta->tldata[tid].slabsize = kSlabSize;          // pstore()
                meta->top.pstore(meta->top.pload() + kSlabSize); // pstore()
                // TODO: add the remains of the slab to thread-local freelist
            }
            // Take space from the slab
            myblock = (block*)meta->tldata[tid].slabtop.pload();
            myblock->size2 = bsize;                                      // pstore()
            meta->tldata[tid].slabtop.pstore((uint8_t*)myblock + asize); // pstore()
            meta->tldata[tid].slabsize -= asize;                         // pstore()
        } else if (!meta->gflists[bsize].isEmpty()) {
            if (debugOn) printf("Found available block in thread-global freelist\n");
            myblock = meta->gflists[bsize].popBlock();
        } else {
            if (debugOn) printf("Creating new block from top, currently at %p\n", meta->top.pload());
            // Couldn't find a suitable block, get one from the top of the pool if there is one available
            if (meta->top.pload() + asize > poolSize + poolAddr) {
                printf("EsLoco2: Out of memory for %ld bytes allocation\n", size);
                return nullptr;
            }
            myblock = (block*)meta->top.pload();
            meta->top.pstore(meta->top.pload() + asize);                 // pstore()
            myblock->size2 = bsize;                                       // pstore()
        }
        if (debugOn) printf("returning ptr = %p\n", (void*)((uint8_t*)myblock + sizeof(block)));
        //printf("malloc:%ld = %p\n", asize, (void*)((uint8_t*)myblock + sizeof(block)));
        // Return the block, minus the header
        return (void*)((uint8_t*)myblock + sizeof(block));
    }

    // Takes a pointer to an object and puts the block on the free-list.
    // When the thread-local freelist becomes too large, all blocks are moved to the thread-global free-list.
    // Does on average 2 stores to persistent memory.
    void free(void* ptr) {
        if (ptr == nullptr) return;
        // const int tid = ThreadRegistry::getTID();
        const int tid = ((Thread_void*)(__tm_self))->UniqID;        
        // const int tid = 0;
        block* myblock = (block*)((uint8_t*)ptr - sizeof(block));
        if (debugOn) printf("free:%ld %p\n", 1UL << myblock->size2.pload(), ptr);
        // Insert the block in the corresponding freelist
        uint64_t bsize = myblock->size2;
        meta->tldata[tid].tflists[bsize].pushBlock(myblock);
        if (meta->tldata[tid].tflists[bsize].isFull()) {
            if (debugOn) printf("Moving thread-local flist for bsize=%ld to thread-global flist\n", bsize);
            // Move all objects in the thread-local list to the corresponding thread-global list
            meta->tldata[tid].tflists[bsize].transfer(&meta->gflists[bsize]);
        }
    }
};




#define VMEM_FILE_PATH "/dev/shm/phytm1_vmem"
#define VMEM_POOL_BASE_ADDR 0x7f0000000000


#define VMEM_POOL_SIZE 1*1024*1024*1024ULL 
#include "nvram_file_mapping.h"

EsLoco2<tx_field> esloco;

void tmInit() {
    printf("Initializing TM\n");

    struct stat buf;
	int vmemFD;
    if (stat(VMEM_FILE_PATH, &buf) != 0) { //file does not exist
		createFile(vmemFD, VMEM_FILE_PATH, VMEM_POOL_SIZE, (void*)VMEM_POOL_BASE_ADDR);
	}
    else { //file exists             
        if (remove(VMEM_FILE_PATH) != 0) {
            printf("Error removing old vmem file\nExiting\n");
            exit(-1);
        }
        printf("Removed old vmem file\n");        
        createFile(vmemFD, VMEM_FILE_PATH, VMEM_POOL_SIZE, (void*)VMEM_POOL_BASE_ADDR);
    }

    void* vmemBaseAddr;
    vmemBaseAddr = mmapFile(vmemFD, VMEM_POOL_SIZE, (void*)VMEM_POOL_BASE_ADDR, MAP_SHARED);      

    printf("Vmem base addr: %p\n", vmemBaseAddr);
    printf("Vmem end addr: %p\n", (void*)((uint64_t)vmemBaseAddr + VMEM_POOL_SIZE));
    printf("Vmem pool size: %llu\n", VMEM_POOL_SIZE);
    
    esloco.init(vmemBaseAddr, VMEM_POOL_SIZE, true);    
}

template <typename T, typename... Args> static T* tmNew(Args&&... args) {
    if (!__tm.tx_active) {
        printf("ERROR: tmNew: Can not allocate outside a transaction\n");
        exit(-1);
        return nullptr;
    }
    void* ptr = esloco.malloc(sizeof(T));
    // If we get nullptr then we've ran out of PM space
    assert(ptr != nullptr);
    // If there is an abort during the 'new placement', the transactions rolls
    // back and will automatically revert the changes in the allocator metadata.
    new (ptr) T(std::forward<Args>(args)...);  // new placement
    return (T*)ptr;
}

template<typename T> static void tmDelete(T* obj) {
    if (obj == nullptr) return;
    if (!__tm.tx_active) {
        printf("ERROR: tmDelete: Can not de-allocate outside a transaction\n");
        exit(-1);
        return;
    }
    obj->~T();
    esloco.free(obj);
}

static void* tmMalloc(size_t size) {
    if (!__tm.tx_active) {
        printf("ERROR: tmMalloc: Can not allocate outside a transaction\n");
        exit(-1);
        return nullptr;
    }
    void* obj = esloco.malloc(size);
    return obj;
}

static void tmFree(void* obj) {
    if (obj == nullptr) return;
    if (!__tm.tx_active) {
        printf("ERROR: tmFree: Can not de-allocate outside a transaction\n");
        exit(-1);
        return;
    }
    esloco.free(obj);
}

static void* pmalloc(size_t size) {
    if (!__tm.tx_active) {
        printf("ERROR: pmalloc: Can not allocate outside a transaction\n");
        exit(-1);
        return nullptr;
    }
    return esloco.malloc(size);
}

static void pfree(void* obj) {
    if (obj == nullptr) return;
    if (!__tm.tx_active) {
        printf("ERROR: pfree: Can not de-allocate outside a transaction\n");
        exit(-1);
        return;
    }
    esloco.free(obj);
}

#ifndef CTASSERT
    #define CTASSERT(x)                 ({ int a[1-(2*!(x))] __attribute((unused)); a[0] = 0;})
#endif

#endif	/* DSBENCH_TM_H */
