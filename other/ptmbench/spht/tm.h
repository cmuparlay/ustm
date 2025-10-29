#ifndef TM_H
#define TM_H 1

#  include <stdio.h>

#ifndef REDUCED_TM_API

#  define MAIN(argc, argv)              int main (int argc, char** argv)
#  define MAIN_RETURN(val)              return val

#  define TM_PRINTF                     printf
#  define TM_PRINT0                     printf
#  define TM_PRINT1                     printf
#  define TM_PRINT2                     printf
#  define TM_PRINT3                     printf


#  include <assert.h>
#  include "memory.h"

#  include <math.h>

#include "spins.h"
#include "impl.h"
#include "htm_impl.h"

#  define TM_ARG                         /* nothing */
#  define TM_ARG_ALONE                   /* nothing */
#  define TM_ARGDECL                     /* nothing */
#  define TM_ARGDECL_ALONE               /* nothing */
#  define TM_CALLABLE                    /* nothing */

// outside the TX
# define S_MALLOC                       nvmalloc
# define S_FREE                         nvfree

# define P_MALLOC(_size)                ({ void *_PTR = nvmalloc_local(HTM_SGL_tid, _size); /*onBeforeWrite(HTM_SGL_tid, _ptr, 0);*/ _PTR; })
# define P_MALLOC_THR(_size, _thr)      ({ void *_PTR = nvmalloc_local(_thr, _size); /*onBeforeWrite(HTM_SGL_tid, _ptr, 0);*/ _PTR; })
# define P_FREE(ptr)                    nvfree(ptr)

// inside the TX
# define TM_MALLOC(_size)               \
({ \
  void *_PTR; \
  _PTR = nvmalloc_local(HTM_SGL_tid, _size); \
  onBeforeWrite(HTM_SGL_tid, _PTR, 0); \
  _PTR; \
})
# define TM_FREE(ptr)                   ({ onBeforeWrite(HTM_SGL_tid, ptr, 0); nvfree(ptr); })

# define SETUP_NUMBER_TASKS(n)
# define SETUP_NUMBER_THREADS(n)
# define PRINT_STATS()
# define AL_LOCK(idx)

#endif


#ifndef SOFTWARE_BARRIER
    #define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

// #ifdef PROFILE_FALLBACK_PATH_TIME
// #  define TM_THREAD_EXIT_PROFILE(tid) \
//   state_profile(tid);
// #else 
// #  define TM_THREAD_EXIT_PROFILE(tid) 
// #endif

#  define TM_THREAD_EXIT_PROFILE() 


# define TM_STARTUP(numThread, tid) \
  SOFTWARE_BARRIER; \
  HTM_init(numThread); \
  guy_configSetup(tid); \
  global_structs_init( \
    numThread, \
    numThread, \
    2 /* NB_EPOCHS: use max of 1073741824 */, \
    67108864L/*268435456L /* LOG_SIZE: in nb of entries */, \
    2281701376L /* LOCAL_MEM_REGION (500M) */, \
    8589934592L /* SHARED_MEM_REGION (1G) */, \
    NVRAM_REGIONS \
  ); \
  SOFTWARE_BARRIER;
  

# define TM_SHUTDOWN() \
  SOFTWARE_BARRIER; \
  global_structs_destroy(); \
  SOFTWARE_BARRIER;

# define TM_THREAD_ENTER() \
  HTM_thr_init(-1); \
  HTM_set_is_record(1); 

# define TM_THREAD_EXIT() \
  TM_THREAD_EXIT_PROFILE(); \
  HTM_thr_exit(); 

# define TM_BEGIN() \
  NV_HTM_BEGIN(HTM_SGL_tid); 

# define TM_END() \
  NV_HTM_END(HTM_SGL_tid); 

# define TM_RESTART()                  HTM_abort();

# define TM_LOCAL_WRITE(var, val)      ({ var = val; var; })
# define TM_LOCAL_WRITE_P(var, val)    ({ var = val; var; })
# define TM_LOCAL_WRITE_D(var, val)    ({ var = val; var; })

# define TM_SHARED_READ(var) (var)
# define TM_SHARED_READ_P(var) (var)
# define TM_SHARED_READ_D(var) (var)
# define TM_SHARED_READ_F(var) (var)
# define TM_SHARED_WRITE(var, val)   ({ onBeforeWrite(HTM_SGL_tid, &var, val); var = val; var;})
# define TM_SHARED_WRITE_P(var, val) ({ onBeforeWrite(HTM_SGL_tid, &var, val); var = val; var;})
# define TM_SHARED_WRITE_D(var, val) ({ onBeforeWrite(HTM_SGL_tid, &var, val); var = val; var;})
# define TM_SHARED_WRITE_F(var, val) ({ onBeforeWrite(HTM_SGL_tid, &var, val); var = val; var;})

// ----------------------------------------------
#define SLOW_PATH_SHARED_READ TM_SHARED_READ
#define FAST_PATH_SHARED_READ TM_SHARED_READ
#define SLOW_PATH_SHARED_READ_D TM_SHARED_READ_D
#define FAST_PATH_SHARED_READ_D TM_SHARED_READ_D
#define SLOW_PATH_SHARED_READ_P TM_SHARED_READ_P
#define FAST_PATH_SHARED_READ_P TM_SHARED_READ_P

#define SLOW_PATH_SHARED_WRITE TM_SHARED_WRITE
#define FAST_PATH_SHARED_WRITE TM_SHARED_WRITE
#define SLOW_PATH_SHARED_WRITE_D TM_SHARED_WRITE_D
#define FAST_PATH_SHARED_WRITE_D TM_SHARED_WRITE_D
#define SLOW_PATH_SHARED_WRITE_P TM_SHARED_WRITE_P
#define FAST_PATH_SHARED_WRITE_P TM_SHARED_WRITE_P
// ----------------------------------------------

#ifndef DISABLE_PHTM
  #define USE_PCWM2
  #define USE_LOG_REPLAY_PARALLEL
  #define USE_LOG_REPLAY_CONCURRENT
  #define USE_LOG_REPLAY_BUFFER_WBINVD
#endif

static void resetCounters() {
#ifdef PROFILE_FALLBACK_PATH_TIME
  timeAbortedTX_global = 0;
#endif
}

void(*state_profile)(int);

static void guy_configSetup(int tid)
{  
  printf("tid=%d\n", tid);

#ifdef DISABLE_PHTM
  printf("PHTM is disabled. Using noop bindings.\n");
  log_replay_flags = 0;
  on_before_htm_begin  = on_before_htm_begin_disable_phtm;
  on_htm_abort         = on_htm_abort_disable_phtm;
  on_before_htm_write  = on_before_htm_write_8B__disable_phtm;
  on_before_htm_commit = on_before_htm_commit_disable_phtm;
  on_after_htm_commit  = on_after_htm_commit_disable_phtm;
  wait_commit_fn = wait_commit_disable_phtm;

  return;
#endif

  log_replay_flags = LOG_REPLAY_FORWARD;

#if defined(USE_LOG_REPLAY_BACKWARD) || defined (USE_PCWM3)
  log_replay_flags = LOG_REPLAY_BACKWARD;
  printf("LOG_REPLAY_BACKWARD is set\n");
#endif

#if defined(USE_PCWM)  
  printf("usePCWM is set\n");
  install_bindings_pcwm();
  wait_commit_fn = wait_commit_pcwm;
  log_replay_flags |= LOG_REPLAY_PHYSICAL_CLOCKS;
  state_print_profile = state_fprintf_profiling_info_pcwm;
#elif defined(USE_PCWM2)
  printf("usePCWM2 is set\n");
  install_bindings_pcwm2();
  wait_commit_fn = wait_commit_pcwm2;
  log_replay_flags |= (int)LOG_REPLAY_PHYSICAL_CLOCKS_SORTED;  
  state_profile = state_gather_profiling_info_pcwm2;
#elif defined(USE_PCWM3)
  printf("usePCWM3 is set\n");
  install_bindings_pcwm3();
  wait_commit_fn = wait_commit_pcwm3;
  log_replay_flags |= LOG_REPLAY_PHYSICAL_CLOCKS_SORTED_BACKWARD;
  state_profile = state_gather_profiling_info_pcwm3;
#endif
  
#if defined(USE_LOG_REPLAY_BUFFER_WBINVD)
  log_replay_flags |= LOG_REPLAY_BUFFER_WBINVD;
  printf("LOG_REPLAY_BUFFER_WBINVD is set\n");
#elif defined(USE_LOG_REPLAY_BUFFER_FLUSHES)
  log_replay_flags |= LOG_REPLAY_BUFFER_FLUSHES;
  printf("LOG_REPLAY_BUFFER_FLUSHES is set\n");
#elif defined (USE_LOG_REPLAY_RANGE_FLUSHES)
  log_replay_flags |= LOG_REPLAY_RANGE_FLUSHES;
  printf("LOG_REPLAY_RANGE_FLUSHES is set\n");
#endif
  
#if defined(USE_LOG_REPLAY_ASYNC_SORTER)
  log_replay_flags |= LOG_REPLAY_ASYNC_SORTER;
  printf("LOG_REPLAY_ASYNC_SORTER is set\n");
#endif  

#if defined(USE_LOG_REPLAY_PARALLEL)
  log_replay_flags |= (int)LOG_REPLAY_PARALLEL;
  printf("LOG_REPLAY_PARALLEL is set\n");
#endif

#if defined(USE_LOG_REPLAY_CONCURRENT)
  printf("LOG_REPLAY_CONCURRENT is set\n");
  log_replay_flags |= (int)LOG_REPLAY_CONCURRENT;
#endif  

#if defined(DISABLE_LOG_REPLAY)
  printf("DISABLE_LOG_REPLAY is set\n");
  log_replay_flags = 0;
#endif  

  printf(" --- \n");
}




template <typename T>
struct tx_field {    
    uintptr_t bits;

    __attribute__((always_inline)) tx_field() {
        // if constexpr (sizeof(T) > sizeof(uintptr_t)) {
        //     fprintf(stderr, "\nFATAL ERROR: tx_field has type T larger than uintptr_t\n\n");
        //     // Debug::DeathHandler dh; dh.set_append_pid(true);
        //     abort();
        // }
    }

    __attribute__((always_inline)) inline T pload() {
        return load();
    }

    __attribute__((always_inline)) inline T load() {
        T retval = (T) (uint64_t) TM_SHARED_READ_P(bits);
        return retval;
    }
    __attribute__((always_inline)) inline operator T() {
        return load();
    }
    __attribute__((always_inline)) inline T operator->() {
        assert(std::is_pointer<T>::value);
        return *this;
    }

    __attribute__((always_inline)) inline void pstore(T other) {
        store(other);
    }

    __attribute__((always_inline)) inline T store(T const other) {
        TM_SHARED_WRITE_P(bits, (uint64_t) other);
        return (T) (uint64_t) other;
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

template <typename T, typename... Args> static T* tmNew(Args&&... args) {
    void* ptr = TM_MALLOC(sizeof(T));
    // If we get nullptr then we've ran out of PM space
    assert(ptr != nullptr);
    new (ptr) T(std::forward<Args>(args)...);  // new placement
    return (T*)ptr;
}

template<typename T> static void tmDelete(T* obj) {
    if (obj == nullptr) return;    
    obj->~T();
    TM_FREE(obj);
}

static void* tmMalloc(size_t size) {   
    void* obj = TM_MALLOC(size);
    return obj;
}

static void tmFree(void* obj) {
    if (obj == nullptr) return;
    TM_FREE(obj);
}


static void* pmalloc(size_t size) {
    return TM_MALLOC(size);
}

static void pfree(void* obj) {    
    if (obj == nullptr) return;
    TM_FREE(obj);
}



#endif
