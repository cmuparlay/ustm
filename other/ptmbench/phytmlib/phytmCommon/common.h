#ifndef PHYTM_COMMON
#define PHYTM_COMMON

#include "user_gstats_handler.h"

#ifndef TM_GSTATS
#define TM_GSTATS
#endif

#ifndef GSTATS_HANDLE_STATS
#undef TM_GSTATS
#endif

#define _always_inline inline __attribute__((always_inline))

#define COMBINE_PVER_AND_THREAD_ID(pver, tid) ((pver << 8) | tid)


#ifndef PAD
#define CAT2(x, y) x##y
#define CAT(x, y) CAT2(x, y)
#define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#ifndef FAA
// __sync_fetch_and_add (type *ptr, type value, ...)
#define FAA __sync_fetch_and_add 
#endif

#define BCAS(ptr, oldVal, newVal) __sync_bool_compare_and_swap(ptr, oldVal, newVal);
#define PHYTM_FAA(ptr, val) __sync_fetch_and_add(ptr, val)


#include "config.h"
#include "nvram_file_mapping.h"
#include "htmWriteSetEntry.h"
#include "abortTypes.h"
#include "../tx_tuple.h"

#define USE_TM_MEM_MANAGER 1
#ifdef USE_TM_MEM_MANAGER
    #include "tm_memory_manager.h"
    extern MemoryManager gMemManager;
    extern thread_local AllocInfo* tl_allocInfo;
#else 
    #ifndef USE_ESLOCO2
        #include "tm_alloc2.h"  
        extern TMAlloc tmAlloc;
    #endif
#endif

extern bool tooManyAbortsFlag;

PAD;
static void* vmemBaseAddr_numa0;
static void* vmemBaseAddr_numa1;
PAD;
static void* pmemBaseAddr_numa0;
static void* pmemBaseAddr_numa1;
PAD;
#ifdef PHYTM_TRACK_MAX_SET_SIZES
    padded_uint64t maxWrSetSize[MAX_THREADS_POW2];
    padded_uint64t maxRdSetSize[MAX_THREADS_POW2];
    bool abortPrinted = false;
    PAD;
#endif

struct padded_uint64t {
    uint64_t data;
    uint64_t padding[31]; //PMEM padding - 256 byte blocks
};
PAD;

struct pmemTuple {
    intptr_t old;
    intptr_t val;    
    uint64_t pversionAndThreadId;
    // uint64_t padding[5];
} __attribute__((packed));
// struct pmemTuple {
//     intptr_t old;
//     intptr_t val;    
//     uint64_t pversionAndThreadId;
// } __attribute__((packed));
PAD;


#ifndef PMEM_POOL_SIZE
    #define PMEM_POOL_SIZE VMEM_POOL_SIZE * sizeof(pmemTuple)
#endif

// static uint64_t pVersionNums[MAX_THREADS_POW2];
// static padded_uint64t pVersionNums[MAX_THREADS_POW2];
static padded_uint64t* pVersionNums;
// PAD;
static padded_uint64t* p_pVersionNums;
PAD;

#include "../../tmlib/hytm1/counters/counters.h"


//#define DEBUG_PRINT
#define DEBUG_PRINT_LOCK

#ifdef DEBUG_PRINT
    #define aout(x) { \
        cout<<x<<endl; \
    }
#elif defined(DEBUG_PRINT_LOCK)
    #define aout(x) { \
        __acquireLock(&globallock); \
        cout<<x<<endl; \
        __releaseLock(&globallock); \
    }
#else
    #define aout(x)
#endif

#define debug(x) (#x)<<"="<<x
//#define LONG_VALIDATION
#define VALIDATE_INV(x) VALIDATE (x)->validateInvariants()
#define VALIDATE if(0)
#define ERROR(x) { \
    cerr<<"ERROR: "<<x<<endl; \
    printStackTrace(); \
    exit(-1); \
}


// just for debugging
extern volatile int globallock;

extern volatile int lockflag;

#define BIG_CONSTANT(x) (x##LLU)


#include <stdint.h>
#include "../../tmlib/hytm1/platform.h"

#  include <setjmp.h>
#  define SIGSETJMP(env, savesigs)      sigsetjmp(env, savesigs)
#  define SIGLONGJMP(env, val)          siglongjmp(env, val); assert(0)

/*
 * Prototypes
 */

void     TxClearRWSets (void* _Self);
void*    TxNewThread   ();

void     TxFreeThread  (void*);
void     TxInitThread  (void*, long id);
int      TxCommit      (void*);
void     TxAbort       (void*);

intptr_t TxLoad(void* Self, volatile intptr_t* addr);
template <bool isPersistent = false>
void TxStore(void* Self, volatile intptr_t* addr, intptr_t value);

void     TxOnce        ();
void     TxClearCounters();
void     TxShutdown    ();

#endif