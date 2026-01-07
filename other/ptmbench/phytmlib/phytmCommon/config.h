#pragma once

#define DISABLE_PMEM

#ifndef HTM_ATTEMPT_THRESH
    #ifndef DISABLE_HTM
        #define HTM_ATTEMPT_THRESH 40
    #else
        #define HTM_ATTEMPT_THRESH 0
    #endif
#endif
//#define TXNL_MEM_RECLAMATION



// #define NO_RECLAIM
// #define USE_GLOBAL_HTM_WRITE_SET


#ifndef PMEM_FILE_PATH
    #ifndef DISABLE_PMEM
        // #define PMEM_FILE_PATH "/mnt/pmem1_mount/gccoccim/phytm1_pmem"        
        #define PMEM_FILE_PATH_NUMA0 "/mnt/pmem0_mount/gccoccim/phytm_pmem_numa0"        
        #define PMEM_FILE_PATH_NUMA1 "/mnt/pmem1_mount/gccoccim/phytm0_pmem_numa1"        
    #else
        #define PMEM_FILE_PATH_NUMA0 "/dev/shm/phytm1_fake_pmem0"        
        #define PMEM_FILE_PATH_NUMA1 "/dev/shm/phytm1_fake_pmem1"        
    #endif
#endif

#ifndef VMEM_FILE_PATH_NUMA0
    #define VMEM_FILE_PATH_NUMA0 "/dev/shm/phytm_vmem_numa0"
#endif

#ifndef VMEM_FILE_PATH_NUMA1
    #define VMEM_FILE_PATH_NUMA1 "/dev/shm/phytm_vmem_numa1"
#endif

#ifndef PMEM_POOL_BASE_ADDR_NUMA0
    #define PMEM_POOL_BASE_ADDR_NUMA0 0x7ff000000000
#endif

#ifndef PMEM_POOL_BASE_ADDR_NUMA1
    #define PMEM_POOL_BASE_ADDR_NUMA1 0x7fff00000000
#endif

#ifndef VMEM_POOL_BASE_ADDR_NUMA0
    #define VMEM_POOL_BASE_ADDR_NUMA0 0x7f0000000000
#endif

#ifndef VMEM_POOL_BASE_ADDR_NUMA1
    #define VMEM_POOL_BASE_ADDR_NUMA1 0x7fea00000000
#endif

#ifndef VMEM_POOL_SIZE
    #define VMEM_POOL_SIZE 16*1024*1024*1024ULL  // 16GB for USTM benchmarks
    // #define VMEM_POOL_SIZE 4*1024*1024*1024ULL  // 4GB (original)
    // #define VMEM_POOL_SIZE 512*1024*1024ULL 
#endif


#ifndef MAX_THREADS_POW2
    #define MAX_THREADS_POW2 128
#endif

#ifndef MAX_HTM_WRITE_SET_SIZE 
    #define MAX_HTM_WRITE_SET_SIZE 512
#endif

#ifndef MAX_RETRIES
    #define MAX_RETRIES 10000000
#endif

#ifndef MAX_FALLBACK_RETRIES_BEFORE_SORTING
	#define MAX_FALLBACK_RETRIES_BEFORE_SORTING 100
#endif

#if !defined(DISABLE_PMEM) && !defined(USE_NOOP_FLUSHES_AND_FENCES)    
    #if !defined(USE_CLFLUSHOPT) && !defined(USE_CLWB) && !defined(USE_CLFLUSH)
        #define USE_CLFLUSHOPT
        // #define USE_CLFLUSH
    #endif

    #if defined(USE_CLFLUSHOPT)
        #define FLUSH(p) {asm volatile("clflushopt (%0)" :: "r"(p));}
        #define SFENCE {asm volatile("sfence" ::: "memory");}
        // #warning "Using CLFLUSHOPT"
    #elif defined(USE_CLWB) 
        // #define FLUSH(p) {asm volatile("clwb (%0)" :: "r"(p));}	
        #define FLUSH(addr) {__asm__ volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr))); }
        #define SFENCE {asm volatile("sfence" ::: "memory");}
        // #warning "Using CLWB"
    #else
        #define FLUSH(p) {asm volatile("clflush (%0)" :: "r"(p));}        
        #define SFENCE
        // #warning "Using CLFLUSH"
    #endif

    #ifdef USE_GLOBAL_HTM_WRITE_SET    
        static volatile intptr_t* htmWriteSet[MAX_THREADS_POW2][MAX_HTM_WRITE_SET_SIZE];
        static uint64_t htmWriteSetSize[MAX_THREADS_POW2];
    #endif
#else 
    #define FLUSH(p) 
    #define SFENCE
#endif