/*
 * File:   dsbench_tm.h
 * Author: trbot
 *
 * Created on December 14, 2016, 4:01 PM
 */

#ifndef DSBENCH_TM_H
#define	DSBENCH_TM_H

// #ifdef DISABLE_PMEM
//     #warning "NVRAM disabled"
// #endif

// #ifdef DISABLE_HTM
//     #warning "HTM disabled"
// #endif

#include <stdio.h>
#include <string.h>
#include <type_traits> // for is_pointer<T>
#include <execinfo.h>
#include <cstring> //for memset
// #include "death_handler.h"


bool tooManyAbortsFlag = false;

#define USE_TM_MEM_MANAGER 1
// #define USE_ESLOCO2

// #define PHYTM_TRACK_MAX_SET_SIZES

// #define DISALLOW_UNSAFE
// #define ESLOCO_ENABLE_DEFERRED_FREE
// #define ENABLE_UNSAFE_WRITES_COMMIT_TIME_FLUSH 1

#ifdef USE_ESLOCO2
#define DISALLOW_UNSAFE
#ifndef MAX_DEFERRED_FREES
    #define MAX_DEFERRED_FREES 512
#endif
#endif

#define DISABLE_TM_COUNTERS

// #define PHYTM_COLOCATED_LOCKS


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

#ifndef PAD
    #define CAT2(x, y) x##y
    #define CAT(x, y) CAT2(x, y)
    #define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#ifndef likely
    #define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
    #define unlikely(x)     __builtin_expect((x),0)
#endif

void tmInit();
void tmInit(int);

#define TM_ARG                          STM_SELF,
#define TM_ARG_ALONE                    STM_SELF
#define TM_ARGDECL                      STM_THREAD_T* TM_ARG
#define TM_ARGDECL_ALONE                STM_THREAD_T* TM_ARG_ALONE

#define TM_INIT()                       tx_active = 0;
#define TM_STARTUP(numThreads)          STM_STARTUP(); 
#define TM_SHUTDOWN()                   STM_SHUTDOWN();

// #define TM_THREAD_ENTER(tid)            TM_ARG_ALONE = STM_NEW_THREAD((tid)); \
//                                         STM_INIT_THREAD(TM_ARG_ALONE, tid)
#define TM_THREAD_ENTER()               TM_ARG_ALONE = STM_NEW_THREAD((tid));
#define TM_THREAD_EXIT()                STM_FREE_THREAD(TM_ARG_ALONE)

#define TM_MALLOC(size)                 STM_MALLOC(TM_ARG_ALONE, size)
#define TM_FREE(ptr)                    STM_FREE(TM_ARG_ALONE, ptr)

#if defined(NDEBUG) && !defined(DISALLOW_UNSAFE)
    #   define TM_BEGIN()                   STM_BEGIN_WR(TM_ARG_ALONE); SOFTWARE_BARRIER;
    #   define TM_BEGIN_UNSAFE()            unsafeMode = true; SOFTWARE_BARRIER;
    #   define TM_BEGIN_RO()                STM_BEGIN_RD(TM_ARG_ALONE); SOFTWARE_BARRIER;
    #if defined(USE_ESLOCO2) && defined(ESLOCO_ENABLE_DEFERRED_FREE) && !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; esloco.doDeferedFrees(); SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  
        #   define TM_END_UNSAFE()              SOFTWARE_BARRIER; esloco.doDeferedFrees(); unsafeMode = false; SOFTWARE_BARRIER; SFENCE;
    #elif !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  
        #   define TM_END_UNSAFE()              unsafeMode = false; SOFTWARE_BARRIER; SFENCE;
    #else
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE); 
        #   define TM_END_UNSAFE()              unsafeMode = false; SOFTWARE_BARRIER; SFENCE;
    #endif
#elif !defined(NDEBUG) && !defined(DISALLOW_UNSAFE)
    #   define TM_BEGIN()                   STM_BEGIN_WR(TM_ARG_ALONE); tx_active = 1; SOFTWARE_BARRIER;    
    #   define TM_BEGIN_UNSAFE()            unsafeMode = true; tx_active = 1; SOFTWARE_BARRIER;
    #   define TM_BEGIN_RO()                STM_BEGIN_RD(TM_ARG_ALONE); tx_active = 1; SOFTWARE_BARRIER;
    #if defined(USE_ESLOCO2) && defined(ESLOCO_ENABLE_DEFERRED_FREE) && !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; esloco.doDeferedFrees(); SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              SOFTWARE_BARRIER; esloco.doDeferedFrees(); unsafeMode = false; SOFTWARE_BARRIER; SFENCE; tx_active = 0;
    #elif !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              unsafeMode = false; SOFTWARE_BARRIER; SFENCE; tx_active = 0;    
    #else
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              unsafeMode = false; SOFTWARE_BARRIER; SFENCE; tx_active = 0;
    #endif
#endif

#if defined(NDEBUG) && defined(DISALLOW_UNSAFE)
    #   define TM_BEGIN()                   STM_BEGIN_WR(TM_ARG_ALONE); SOFTWARE_BARRIER;
    #   define TM_BEGIN_UNSAFE()            TM_BEGIN() 
    #   define TM_BEGIN_RO()                STM_BEGIN_RD(TM_ARG_ALONE); SOFTWARE_BARRIER;
    #if defined(USE_ESLOCO2) && defined(ESLOCO_ENABLE_DEFERRED_FREE) && !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; esloco.doDeferedFrees(); SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE); 
        #   define TM_END_UNSAFE()              TM_END()
    #elif !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);
        #   define TM_END_UNSAFE()              TM_END()
    #else
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);
        #   define TM_END_UNSAFE()              TM_END()
    #endif
#elif !defined(NDEBUG) && defined(DISALLOW_UNSAFE)
    #   define TM_BEGIN()                   STM_BEGIN_WR(TM_ARG_ALONE); tx_active = 1; SOFTWARE_BARRIER;    
    #   define TM_BEGIN_UNSAFE()            TM_BEGIN() 
    #   define TM_BEGIN_RO()                STM_BEGIN_RD(TM_ARG_ALONE); tx_active = 1; SOFTWARE_BARRIER;
    #if defined(USE_ESLOCO2) && defined(ESLOCO_ENABLE_DEFERRED_FREE) && !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; esloco.doDeferedFrees(); SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              TM_END()
    #elif !defined(DISABLE_TM_ALLOCATOR)
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              TM_END()
    #else
        #   define TM_END()                     SOFTWARE_BARRIER; STM_END(TM_ARG_ALONE);  tx_active = 0;
        #   define TM_END_UNSAFE()              TM_END()
    #endif
#endif


#define TM_RESTART()                    STM_RESTART(TM_ARG_ALONE)

#define TM_SHARED_READ_P(var)           STM_READ_P(TM_ARG_ALONE, var)
#define TM_SHARED_WRITE_P(var, val)     STM_WRITE_P(TM_ARG_ALONE, (var), val)

#if defined(phytm1)
    // #include "phytmCommon/stmCommon.h"
    #include "phytm1/stm.h"
    #include "phytm1/phytm1.h"
    #include "phytm1/phytm1.cpp"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1
    #endif
#elif defined(phytm1_fgl)
    #define TM_NAME "phytm1_fgl"
    #ifdef USE_GLOBAL_LOCK
        #undef USE_GLOBAL_LOCK
    #endif
    #include "phytmCommon/stmCommon.h"    
    #include "phytmCommon/tm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl
    #endif
#elif defined(phytm1_fgl_colocated_locks)
    #define PHYTM_COLOCATED_LOCKS
    #define TM_NAME "phytm1_fgl_colocated_locks"
    #ifdef USE_GLOBAL_LOCK
        #undef USE_GLOBAL_LOCK
    #endif

    #include "phytmCommon/stmCommon.h"            
    #include "phytmCommon/tm.h"

    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_colocated_locks
    #endif
#elif defined(phytm1_fgl_wrset_sorting)
    #define TM_NAME "phytm1_fgl_and_rdwrset_sorting"
    #ifndef MAX_FALLBACK_RETRIES_BEFORE_SORTING
        #define MAX_FALLBACK_RETRIES_BEFORE_SORTING 100
    #endif
    #define ENABLE_WRITE_SET_SORTING 1
    #include "phytmCommon/stmCommon.h"
    #include "phytmCommon/tm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl_wrset_sorting
    #endif
#elif defined(phytm1_fgl_always_sorting)
    #define TM_NAME "phytm1_fgl_and_rdwrset_sorting"
    #define ENABLE_WRITE_SET_SORTING 1
    #define FORCE_WRITE_SET_SORTING 1
    #include "phytmCommon/stmCommon.h"    
    #include "phytmCommon/tm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl_always_sorting
    #endif
#elif defined(phytm1_fgl_always_sorting_strong_progressive)
    #define TM_NAME "phytm1_fgl_always_sorting_strong_progressive"
    #define ENABLE_WRITE_SET_SORTING 1
    #define FORCE_WRITE_SET_SORTING 1
    #define STRONG_PROG 1
    #include "phytmCommon/stmCommon.h"
    #include "phytmCommon/tm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl_always_sorting_strong_progressive
    #endif
#elif defined(phytm1_fgl_strong_progressive)
    #define TM_NAME "phytm1_fgl_strong_progressive"
    #define ENABLE_WRITE_SET_SORTING 1
    #define STRONG_PROG 1

    #include "phytmCommon/stmCommon.h"
    #include "phytmCommon/tm.h"    
    
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl_strong_progressive
    #endif
#elif defined(ptm_no_htm)
    #ifndef DISABLE_HTM
        #define DISABLE_HTM
    #endif
    #include "phytmCommon/stmCommon.h"
    #include "ptm_no_htm/ptm_no_htm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl_strong_progressive
    #endif
#elif defined(ptm_no_htm_strong_prog)
    #define TM_NAME "ptm_no_htm_strong_prog"
    #ifndef DISABLE_HTM
        #define DISABLE_HTM
    #endif
    #define ENABLE_WRITE_SET_SORTING 1
    #define STRONG_PROG 1

    #include "phytmCommon/stmCommon.h"
    #include "phytmCommon/tm.h"    
    
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using ptm_no_htm_strong_prog
    #endif
#elif defined(phytm1_fgl_colocated_locks_noop_flush_fence)
    #define USE_NOOP_FLUSHES_AND_FENCES    
    #define PHYTM_COLOCATED_LOCKS

    #ifdef USE_GLOBAL_LOCK
    #undef USE_GLOBAL_LOCK
    #endif

    #define TM_NAME "phytm1_fgl_colocated_locks_noop_flush_fence"

    #include "phytmCommon/stmCommon.h"    
    #include "phytmCommon/tm.h"   

    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_colocated_locks with noop flushes and fences
    #endif
#elif defined(phytm1_fgl_colocated_locks_no_pmem)
    #define USE_NOOP_FLUSHES_AND_FENCES    
    #ifndef DISABLE_PMEM    
        #define DISABLE_PMEM    
    #endif
    #define PHYTM_COLOCATED_LOCKS

    #ifdef USE_GLOBAL_LOCK
        #undef USE_GLOBAL_LOCK
    #endif

    #define TM_NAME "phytm1_fgl_colocated_locks_no_pmem"

    #include "phytmCommon/stmCommon.h"        
    #include "phytmCommon/tm.h"

    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_colocated_locks with PMEM disabled and noop flushes and fences
    #endif
#elif defined(phytm1_fgl_colocated_locks_no_phtm)
    #define USE_NOOP_FLUSHES_AND_FENCES    
    #ifndef DISABLE_PMEM    
        #define DISABLE_PMEM    
    #endif       
    #define PHYTM_COLOCATED_LOCKS

    #ifdef USE_GLOBAL_LOCK
        #undef USE_GLOBAL_LOCK
    #endif

    #define TM_NAME "phytm1_fgl_colocated_locks_no_phtm"    

    #include "volatile_phytm/stm_volatile.h"        
    #include "volatile_phytm/tm_volatile.h"  

    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using fully volatile weak progressive phytm1_colocated_locks
    #endif
#else
    // #error MUST DEFINE A PTM IMPLEMENTATION. ONE OF: phytm1
    #warning No PHYTM implementation defined. Using Phytm1
    #define TM_NAME "phytm1_fgl"
    #ifdef USE_GLOBAL_LOCK
        #undef USE_GLOBAL_LOCK
    #endif
    #include "phytmCommon/stmCommon.h"    
    #include "phytmCommon/tm.h"
    #ifdef PHYTM_USE_COMPILER_WARNINGS
        #warning Using phytm1_fgl
    #endif
#endif

#include "tx_tuple.h"


PAD;
volatile int __initter_id_reservations[2*MAX_THREADS_POW2];
volatile int __tm_lock = 0;
volatile int __tm_global_done = 0;
PAD;

// template <typename DUMMY=int> // to prevent me from having to compile this code outside of the h-file...
// class TMThreadContext {
//     public:
//     PAD;
//     int initter_id;
//     TM_ARGDECL_ALONE;
//     bool tx_active = 0;
//     PAD;

//     TMThreadContext() {
//         tx_active = 0;

//         // ONE thread does global init
//         // printf("TMThreadContext: waiting on lock\n");
//         while (__tm_lock || !__sync_bool_compare_and_swap(&__tm_lock, 0, 1)) {}
//         // printf("TMThreadContext: got lock\n");

//         // lock acquired
//         if (!__tm_global_done) {
//             // printf("TMThreadContext: global initialization\n");
//             __tm_global_done = 1;
//             for (int i=0;i<2*MAX_THREADS_POW2;++i) {
//                 __initter_id_reservations[i] = 0;
//             }
//             TM_STARTUP();
//             __sync_synchronize();
//         }
//         __tm_lock = 0;
//         __sync_synchronize();

//         // EACH thread does local init
//         initter_id = -1;
//         for (int i=0;i<2*MAX_THREADS_POW2;++i) {
//             if (__sync_bool_compare_and_swap(&__initter_id_reservations[i], 0, 1)) {
//                 initter_id = i;
//                 break;
//             }
//         }
//         if (initter_id < 0) {
//             printf("ERROR: initter_id not set. could be that MAX_THREADS_POW2 needs to be increased.\n");
//             exit(-1);
//         }
//         // printf("TMThreadContext: thread initialization initter_id=%d\n", initter_id);
//         // TM_THREAD_ENTER(initter_id); // SETS TM_ARGDECL_ALONE !!!
//         TM_THREAD_ENTER(); // SETS TM_ARGDECL_ALONE !!!
//         // printf("TMThreadContext: entered initter_id=%d\n", initter_id);
//     }
//     ~TMThreadContext() {
//         // printf("~TMThreadContext: destructor for initter_id=%d\n", initter_id);
//         // __sync_synchronize();
//         __initter_id_reservations[initter_id] = 0;
//         __sync_synchronize();
//         TM_THREAD_EXIT();
//     }
// };
// thread_local TMThreadContext<> __tm;
// thread_local int const __tm_id = __tm.initter_id;
// thread_local void * const TM_ARG_ALONE = __tm.TM_ARG_ALONE;
thread_local bool unsafeMode = false;
thread_local TM_ARGDECL_ALONE;
thread_local bool tx_active = 0;

PAD;

void __attribute__((destructor)) __tm_global_destructor() {
    TM_SHUTDOWN();
}


template <typename T>
struct tx_field {
    tx_tuple<vLock> data;
    // T bits;

    __attribute__((always_inline)) tx_field() {
        if constexpr (sizeof(T) > sizeof(uintptr_t)) {
            fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
            abort();
        }
    }

    __attribute__((always_inline)) inline T load_unsafe() {
        #ifdef DISALLOW_UNSAFE
        // return load();
        T retval = (T) (uintptr_t) TM_SHARED_READ_P(data.bits);
        return retval;
        #else
        SOFTWARE_BARRIER;
        return (T) data.bits;
        #endif
    }

    __attribute__((always_inline)) inline T pload() {
        return load();
    }

    __attribute__((always_inline)) inline T load() {
        #if !defined(NDEBUG)
        SOFTWARE_BARRIER;
        // printf("__tm thread %3d: safe load %p\n", __tm.initter_id, &bits);
        if (!tx_active) {
            fprintf(stderr, "\nFATAL ERROR: transactional loads must be performed in an active transaction\n\n");
            // Debug::DeathHandler dh; dh.set_append_pid(true);
            abort();
        }
        #endif

        if (unsafeMode) {
            return load_unsafe();            
        }

        T retval = (T) (uintptr_t) TM_SHARED_READ_P(data.bits);
        return retval;
    }
    __attribute__((always_inline)) inline operator T() {
        return load();
    }
    __attribute__((always_inline)) inline T operator->() {
        assert(std::is_pointer<T>::value);
        return *this;
    }

    __attribute__((always_inline)) inline T store_unsafe(T const other) {
        #ifdef DISALLOW_UNSAFE
        TM_SHARED_WRITE_P(data.bits, (void *) (uintptr_t) other);
        return (T) (uintptr_t) other;
        // store(other);
        // return (T) (uintptr_t) other;
        #else        
        SOFTWARE_BARRIER;

            #if !defined(phytm1_fgl_colocated_locks_no_phtm)
                #if !defined(DISABLE_TM_ALLOC) && !defined(ENABLE_UNSAFE_WRITES_COMMIT_TIME_FLUSH) 
                    const int tid = ((Thread_void*)(TM_ARG_ALONE))->UniqID;    
                    pmemTuple* pmemData = (pmemTuple*)vmemAddrToPmemAddr((void*)&(data.bits));
                    // pmemData->old = data.bits;        
                    // SOFTWARE_BARRIER;   
                    pmemData->pversionAndThreadId = COMBINE_PVER_AND_THREAD_ID(pVersionNums[tid].data, tid);
                    SOFTWARE_BARRIER;
                    pmemData->val = (uintptr_t) other;
                    FLUSH(pmemData);
                #else 
                    STM_UNSAFE_WRITE_P(TM_ARG_ALONE, data.bits, (void *) (uintptr_t) other);
                #endif        
            #endif
        
        //the unsafe store
        data.bits = (uintptr_t) other;
        return (T) (uintptr_t) other;
        #endif
    }

    __attribute__((always_inline)) inline void pstore(T other) {
        store(other);
    }

    __attribute__((always_inline)) inline T store(T const other) {
        #if !defined(NDEBUG)
        SOFTWARE_BARRIER;        
        if (!tx_active) {
            fprintf(stderr, "\nFATAL ERROR: transactional stores must be performed in an active transaction\n\n");            
            abort();
        }
        #endif

        if (unsafeMode) {
            return store_unsafe(other);            
        }

        TM_SHARED_WRITE_P(data.bits, (void *) (uintptr_t) other);
        return (T) (uintptr_t) other;
    
    }
    __attribute__((always_inline)) inline T operator=(T const other) {
        return store(other);
    }
    
    // Copy constructor
    inline tx_field<T>(const tx_field<T>& other) { 
        pstore(other.pload()); 
    }

    inline tx_field<T>& operator=(tx_field<T>& other) {
        pstore(other.pload());
        return *this;
    }

    // // Assignment operator from a value
    // inline tx_field<T>& operator=(T value) {
    //     pstore(value);
    //     return *this;
    // }

    // inline tx_field<T> operator=(tx_field<T> const & other) {
    //     fprintf(stderr, "\nFATAL ERROR: your code tries to assign a value of type tx_field<mode,T> to a field of type tx_field<mode,T>. this is a mistake. you are likely trying to read one transactional address and assign it to another transactional address in the same line. you can split these reads/writes into separate lines, or call 'field.load()' explicitly on the field you're reading before assigning to another address in the same line. this situation arises because the compiler cannot disambiguate between (1) a copy assignment of one tx_field to another, i.e., an = operator with tx_field as its argument, and (2) a read of a tx_field<mode,T> to produce a T, followed by an = operator with argument T. to see WHERE in your code this is happening, check out the helpful backtrace below... (if there is no backtrace, compile with use_asserts=1 to get a nice colorful one.) WARNING: because of inlining, a stack trace may not take you right to your faulty code... in this case, try compiling with make -j no_optimize=1\n\n");
    //     // Debug::DeathHandler dh; dh.set_append_pid(true);
    //     abort();
    // }
};


// extern __thread int tid;

#ifdef USE_TM_MEM_MANAGER
    #include "phytmCommon/tm_memory_manager.h"
#else
    #ifndef USE_ESLOCO2
        #include "phytmCommon/tm_alloc2.h"
    #else
        #include "esloco.h"
        EsLoco2<tx_field> esloco;
    #endif
#endif


void tmInit(int numThreads) {
    printf("TM: ");
    printf(TM_NAME);
    printf("\n");

    #ifndef DISABLE_HTM
    printf("HTM ENABLED\n");
    #else
    printf("HTM DISABLED\n");
    #endif

    #ifndef DISABLE_PMEM
    printf("PMEM ENABLED\n");
    #else
    printf("PMEM DISABLED\n");
    #endif

    #if !defined(DISABLE_TM_ALLOCATOR) 
        #if defined(USE_ESLOCO2)
            printf("ESLOCO2 ALLOCATOR ENABLED\n");
            #ifdef ESLOCO_ENABLE_DEFERRED_FREE
                printf("ESLOCO DEFERRED FREE ENABLED\n");
            #else
                printf("ESLOCO DEFERRED FREE DISABLED\n");
            #endif
        #else
            printf("SIMPLE TM ALLOCATOR ENABLED\n");
        #endif
    #else
    printf("TM ALLOCATOR DISABLED\n");
    #endif

    #ifdef DISABLE_TM_ALLOC_FREE
    printf("TM FREE DISABLED\n");
    #else
    printf("TM FREE ENABLED\n");
    #endif

    #ifdef PHYTM_COLOCATED_LOCKS
    printf("Phytm colocated locks enabled\n");
    #else
    printf("Phytm colocated locks disabled\n");
    #endif


    printf("Size of tx_tuple<vLock> %ld\n", sizeof(tx_tuple<vLock>));
    printf("Max software path retries: %d\n", MAX_RETRIES);
    
    #ifdef USE_TM_MEM_MANAGER                
        void* vmemAddr0 = initVmem_numa0();
        void* vmemAddr1 = initVmem_numa1();
        // void* pmemAddr_numa0 = initPmem_numa0();
        // void* pmemAddr_numa1 = initPmem_numa1();
        gMemManager.init(vmemAddr0, VMEM_POOL_SIZE, vmemAddr1, VMEM_POOL_SIZE);        
    #else
        void* vmemAddr = initVmem();
        void* pmemAddr = initPmem();

        #ifdef USE_ESLOCO2
        esloco.init(vmemAddr, VMEM_POOL_SIZE, true, MAX_THREADS_POW2);  
        #else 
        tmAlloc.init(vmemAddr, VMEM_POOL_SIZE);
        #endif
    #endif
}

void tmInit() {
    tmInit(MAX_THREADS_POW2);
}

template <typename T, typename... Args> static T* tmNew(Args&&... args) {
    #if !defined(NDEBUG)
    SOFTWARE_BARRIER;
    if (!tx_active) {
        printf("ERROR: tmNew: Can not allocate outside a transaction\n");
        exit(-1);
        return nullptr;
    }
    #endif

    #ifdef USE_TM_MEM_MANAGER
        void* ptr;
        if (unlikely(unsafeMode)) {
            ptr = gMemManager.allocate(sizeof(T));
        }
        else {
            ptr = gMemManager.bufferedAllocate(sizeof(T));
        }        
    #else        
        #ifdef USE_ESLOCO2
        void* ptr = esloco.malloc(sizeof(T));
        #else 
        const int tid = ((Thread_void*)(TM_ARG_ALONE))->UniqID;        
        void* ptr = tmAlloc.tmMalloc(tid, sizeof(T));
        #endif
    #endif

    // If we get nullptr then we've ran out of PM space
    assert(ptr != nullptr);
    // If there is an abort during the 'new placement', the transactions rolls
    // back and will automatically revert the changes in the allocator metadata.
    new (ptr) T(std::forward<Args>(args)...);  // new placement
    return (T*)ptr;
}

template<typename T> static void tmDelete(T* obj) {
    #if !defined(NDEBUG)
    SOFTWARE_BARRIER;
    if (!tx_active) {
        printf("ERROR: tmDelete: Can not allocate outside a transaction\n");
        exit(-1);
        return;
    }
    #endif

    if (obj == nullptr) {
        return;
    }

    obj->~T();
    
    #ifdef USE_TM_MEM_MANAGER
        if (unlikely(unsafeMode)) {
            gMemManager.deallocate(obj);
        }
        else {
            gMemManager.deferFree(obj);
        }
    #else
        #ifdef USE_ESLOCO2
            esloco.free(obj);
        #else     
            #ifndef DISABLE_TM_ALLOC_FREE
            const int tid = ((Thread_void*)(TM_ARG_ALONE))->UniqID;        
            tmAlloc.tmFree(tid, obj);
            #endif
        #endif   
    #endif
}

static void* tmMalloc(size_t size) {
    #if !defined(NDEBUG)
    SOFTWARE_BARRIER;
    if (!tx_active) {
        printf("ERROR: tmMalloc: Can not allocate outside a transaction\n");
        exit(-1);
        return nullptr;
    }
    #endif    
    
    #ifdef USE_TM_MEM_MANAGER
        void* obj;
        if (unlikely(unsafeMode)) {
            obj = gMemManager.allocate(size);
        }
        else {
            obj = gMemManager.bufferedAllocate(size);
        }    
    #else
        #ifdef USE_ESLOCO2
        void* obj = esloco.malloc(size);
        #else 
        const int tid = ((Thread_void*)(TM_ARG_ALONE))->UniqID;        
        void* obj = tmAlloc.tmMalloc(tid, size);
        #endif
    #endif
    return obj;
}

static void tmFree(void* obj) {
    #if !defined(NDEBUG)
    SOFTWARE_BARRIER;
    if (!tx_active) {
        printf("ERROR: tmFree: Can not allocate outside a transaction\n");
        exit(-1);
        return;
    }
    #endif

    if (obj == nullptr) {
        return;
    }

    #ifdef USE_TM_MEM_MANAGER
        if (unlikely(unsafeMode)) {
            gMemManager.deallocate(obj);
        }
        else {
            gMemManager.deferFree(obj);
        }
    #else
        #ifdef USE_ESLOCO2
        esloco.free(obj);
        #else
        #ifndef DISABLE_TM_ALLOC_FREE
        const int tid = ((Thread_void*)(TM_ARG_ALONE))->UniqID;
        tmAlloc.tmFree(tid, obj);
        #endif
        #endif 
    #endif
}

#if defined(USE_ESLOCO2) && defined(ESLOCO_ENABLE_DEFERRED_FREE)
template<typename T>
static void tmDeferredDelete(T* obj) {
    #if !defined(NDEBUG)
    SOFTWARE_BARRIER;
    if (!tx_active) {
        printf("ERROR: tmDelete: Can not allocate outside a transaction\n");
        exit(-1);
        return;
    }
    #endif   
    if (obj == nullptr) return;        
    esloco.deferFree(obj);
}
#endif

static void* pmalloc(size_t size) {
    return tmMalloc(size);
}

static void pfree(void* obj) {
    tmFree(obj);
}

#ifndef CTASSERT
#define CTASSERT(x)                 ({ int a[1-(2*!(x))] __attribute((unused)); a[0] = 0;})
#endif

#endif	/* DSBENCH_TM_H */
