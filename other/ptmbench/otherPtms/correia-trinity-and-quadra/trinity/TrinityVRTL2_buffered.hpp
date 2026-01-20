/*
 * Copyright 2018-2020
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 *
 * This work is published under the MIT license. See LICENSE.txt
 */
#pragma once

#include <atomic>
#include <cassert>
#include <functional>
#include <cstring>
#include <thread>       // Needed by this_thread::yield()
#include <sys/mman.h>   // Needed if we use mmap()
#include <sys/types.h>  // Needed by open() and close()
#include <sys/stat.h>
#include <linux/mman.h> // Needed by MAP_SHARED_VALIDATE
#include <fcntl.h>
#include <unistd.h>     // Needed by close()
#include <filesystem>   // Needed by std::filesystem::space()
#include <type_traits>
#include <sched.h>      // sched_setaffinity()
#include <csetjmp>      // Needed by sigjmp_buf
#include <cstdlib>      // Needed by exit()

#include "../../setbench/common/dcss/dcss_impl.h"
/*
 * <h1> Trinity + Volatile Region + Persistent TL2 + volatile locks</h1>
 *
 * This variant has a volatile replica of 'main', which means we can use ranges and it is much faster.
 * There is a volatile region (VR) and a PM region. Each consecutive 24 bytes (3x64bit words) in the VR region
 * maps to a cacheline in the PM region. A cacheline in the PM region is composed of:
 * main    - 24 bytes
 * back    - 24 bytes
 * seq     - 8 bytes
 * padding - 8 bytes
 *
 * For concurrency we use something similar to TL2, but not quite the same:
 * - Load interposing is made with post-validation, no pre-validation is made. See persistvr::load();
 * - On abort, we must increment the global clock;
 * - A full fence is done before the commit of the transaction in endTx(), to prevent re-ordering
 *   of the last modification in the transaction with the rad-set validation;
 * - The global clock and the locks are all kept in volatile memory for efficiency;
 * - The write-set does not contain any data. On commit, the data is taken from VR and written to PR
 *   using the Trinity algorithm. On abort, the opposite is done;
 *
 * Other characteristics:
 * - Durable commit is done after concurrent commit, without modification on PM unless the tx is (concurrent) committed;
 * - Supports ranges
 * - Has improved error handling in mmap() and file opening
 *
 * See durable transactions paper
 */

#if !defined(PWB_IS_CLFLUSH) && !defined(PWB_IS_CLFLUSHOPT) && !defined(PWB_IS_CLWB) && !defined(PWB_IS_NOP)
#define PWB_IS_CLWB
#endif

// Macros needed for persistence
#ifdef PWB_IS_CLFLUSH
  /*
   * More info at http://elixir.free-electrons.com/linux/latest/source/arch/x86/include/asm/special_insns.h#L213
   * Intel programming manual at https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-optimization-manual.pdf
   * Use these for Broadwell CPUs (cervino server)
   */
  #define PWB(addr)              __asm__ volatile("clflush (%0)" :: "r" (addr) : "memory")                      // Broadwell only works with this.
  #define PFENCE()               {}                                                                             // No ordering fences needed for CLFLUSH (section 7.4.6 of Intel manual)
  #define PSYNC()                {}                                                                             // For durability it's not obvious, but CLFLUSH seems to be enough, and PMDK uses the same approach
#elif defined(PWB_IS_CLWB)
  /* Use this for CPUs that support clwb, such as the SkyLake SP series (c5 compute intensive instances in AWS are an example of it) */
  #define PWB(addr)              __asm__ volatile(".byte 0x66; xsaveopt %0" : "+m" (*(volatile char *)(addr)))  // clwb() only for Ice Lake onwards
  #define PFENCE()               __asm__ volatile("sfence" : : : "memory")
  #define PSYNC()                __asm__ volatile("sfence" : : : "memory")
#elif defined(PWB_IS_NOP)
  /* pwbs are not needed for shared memory persistency (i.e. persistency across process failure) */
  #define PWB(addr)              {}
//   #define PFENCE()               __asm__ volatile("sfence" : : : "memory")
//   #define PSYNC()                __asm__ volatile("sfence" : : : "memory")
  #define PFENCE()               {}
  #define PSYNC()                {}
#elif defined(PWB_IS_CLFLUSHOPT)
  /* Use this for CPUs that support clflushopt, which is most recent x86 */
  #define PWB(addr)              __asm__ volatile(".byte 0x66; clflush %0" : "+m" (*(volatile char *)(addr)))    // clflushopt (Kaby Lake)
  #define PFENCE()               __asm__ volatile("sfence" : : : "memory")
  #define PSYNC()                __asm__ volatile("sfence" : : : "memory")
#else
#error "You must define what PWB is. Choose PWB_IS_CLFLUSHOPT if you don't know what your CPU is capable of"
#endif

namespace trinityvrtl2 {

typedef struct _s128t {
    uint64_t low;
    uint64_t hi;
} s128t;

typedef struct _hashSlot {
    volatile uint64_t versionNum;
    volatile intptr_t* addr;
    volatile intptr_t val;
} HashSlot;


#define MAX_COMMITED_TRANSACTIONS_BEFORE_FLUSH (10000)
#define MAX_TABLE_SIZE_BEFORE_FLUSH ((BUFFER_HASH_TABLE_SIZE / 4) * 3) //75% full

#define DCSS_BITS 1
#define STATE_BITS 3
#define MARK_BITS_COUNT (DCSS_BITS + STATE_BITS)
#define MARK_BITS_MASK (15) //1111
#define MARK_BITS_MASK_NO_DCSS (14) //1110
#define DURABLE_MASK (1 << DCSS_BITS)

#define BUFFER_HASH_TABLE_SIZE 524288 
#define CHUNK_SIZE 1024
#define TOTAL_CHUNKS (BUFFER_HASH_TABLE_SIZE / CHUNK_SIZE)

#define GET_MARKED_VERSION_NUM(s128t_field) (s128t_field.low)
#define GET_MARKED_VERSION_NUM_PTR(s128t_field) (s128t_field->low)
#define GET_MARKED_VERSION_NUM_NO_DCSS(s128t_field) (s128t_field.low & ~1)
#define GET_UNMARKED_VERSION_NUM(s128t_field) (s128t_field.low >> MARK_BITS_COUNT)

// #define GET_CHUNKS_TO_PERSIST(s128t_field) ((uint32_t)s128t_field.hi)
#define GET_CHUNKS_TO_PERSIST(s128t_field) (s128t_field.hi)
#define GET_CHUNKS_TO_WRITEBACK(s128t_field) (GET_CHUNKS_TO_PERSIST(s128t_field) + TOTAL_CHUNKS)

#define GET_UNMARKED_VERSION_NUM_FROM_UINT64(num) (num >> MARK_BITS_COUNT)

// XX0 == not durable
// XX1 == durable 
// 01X == write mode
// 10X == persist mode
// 11X == write back mode
enum hashTableBufferStates {
    NOT_DURABLE             = 0,
    DURABLE                 = (1 << DCSS_BITS),

    WRITE_MODE              = (2 << DCSS_BITS),
    PERSIST_MODE            = (4 << DCSS_BITS),
    WRITE_BACK_MODE         = (6 << DCSS_BITS),

    WRITE_MODE_DURABLE      = (3 << DCSS_BITS),
    PERSIST_MODE_DURABLE    = (5 << DCSS_BITS),
    WRITE_BACK_MODE_DURABLE = (7 << DCSS_BITS)
};

//DWCAS specifically for s128t
inline bool DWCAS_s128t(volatile s128t * src, s128t oldVal, s128t newVal) {
    bool result;
    __asm__ __volatile__ (
        "lock cmpxchg16b %1\n\t"
        "setz %0"       
        : "=q" ( result )
        , "+m" ( *src )
        , "+d" ( oldVal.hi )
        , "+a" ( oldVal.low )
        : "c" ( newVal.hi )
        , "b" ( newVal.low )
        : "cc", "memory"
    );
    return result;
}

//hash func recommended by Trevor
inline uint64_t xorshift64(uint64_t key) {	
    uint64_t x = key;
	x ^= x << 13;
	x ^= x >> 7;
	x ^= x << 17;
	return x;
}

inline int getState(uint64_t vNum) {
    int state = vNum & MARK_BITS_MASK_NO_DCSS;
    return state;
}

inline int getState(s128t vData) {
    return getState(GET_MARKED_VERSION_NUM(vData));
}


//
// User configurable variables.
// Feel free to change these if you need larger transactions, or more threads.
//

// Start address of persistent memory region
#ifndef PM_REGION_BEGIN
#define PM_REGION_BEGIN 0x7fea00000000
#endif
// Size of the persistent memory region
#ifndef PM_REGION_SIZE
#define PM_REGION_SIZE 2*1024*1024*1024ULL 
#endif
// Name of persistent file mapping (back)
#ifndef PM_FILE_NAME
#define PM_FILE_NAME   "/mnt/pmem1_mount/trinity_vr_tl2_guy"
#endif

// Maximum number of registered threads that can execute transactions
static const int REGISTRY_MAX_THREADS = 128;

static uint8_t* PMREGION_ADDR = (uint8_t*)PM_REGION_BEGIN;

// End address of mapped persistent memory
static uint8_t* PM_REGION_END = ((uint8_t*)PMREGION_ADDR+PM_REGION_SIZE);
// Maximum number of root pointers available for the user
static const uint64_t MAX_ROOT_POINTERS = 64;

static const char * VFILE_NAME = "/dev/shm/trinityvrtl2_shared_main_guy";
// Start address of volatile memory region (VR)
static uint8_t* VREGION_ADDR = (uint8_t*)0x7f0000000000;



// TL2 constants
static const uint64_t LOCKED  = 0x8000000000000000ULL;
static const int TX_IS_NONE   = 0;
static const int TX_IS_READ   = 1;
static const int TX_IS_UPDATE = 2;
// Number of striped locks. Each lock protects one PMCacheLine. _Must_ be a power of 2.
static const uint64_t NUM_LOCKS = 4*1024*1024;

// Returns the cache line of the address (this is for x86 only)
#define ADDR2CL(_addr) (uint8_t*)((size_t)(_addr) & (~63ULL))


// Function that hashes a PMCacheLine address to a lock index.
inline static uint64_t hidx(size_t pcl) {
    return ((pcl/256) & (NUM_LOCKS-1)); // One lock per 4 persistent cache lines
}
// A 'Tid-Sequence' is a uint64_t which has a sequence (56 bits) and a thread-id (8 bits)
typedef uint64_t tseq_t;
// Function that returns the tid of a p.seq. The tid is the 8 highest bits of p.seq.
inline static uint64_t tseq2tid(tseq_t lseq) { return (lseq >> (64-8)) & 0xFF; }
// Returns the sequence in a lseq
inline static uint64_t tseq2seq(tseq_t lseq) { return (lseq & 0x00FFFFFFFFFFFFFFULL); }
// Creates a new Tid-sequence
inline static tseq_t composeTseq(uint64_t tid, uint64_t seq) { return ((tid << (64-8)) | seq); }
// Helper functions
inline static bool isLocked(uint64_t sl) { return (sl & LOCKED); }
inline static bool isUnlocked(uint64_t sl) { return !(sl & LOCKED); }
// Helper method: sl is unlocked and lower than the clock, or it's locked by me
static inline bool isUnlockedOrLockedByMe(uint64_t rClock, uint64_t tid, uint64_t sl) {
    return ( (!(sl & LOCKED) && (sl <= rClock)) || (sl == (tid|LOCKED)) );
}


//
// Thread Registry stuff
//
extern void thread_registry_deregister_thread(const int tid);

// An helper class to do the checkin and checkout of the thread registry
struct ThreadCheckInCheckOut {
    static const int NOT_ASSIGNED = -1;
    int tid { NOT_ASSIGNED };
    ~ThreadCheckInCheckOut() {
        if (tid == NOT_ASSIGNED) return;
        thread_registry_deregister_thread(tid);
    }
};

extern thread_local ThreadCheckInCheckOut tl_tcico;

// Forward declaration of global/singleton instance
class ThreadRegistry;
extern ThreadRegistry gThreadRegistry;

/*
 * <h1> Registry for threads </h1>
 *
 * This is singleton type class that allows assignement of a unique id to each thread.
 * The first time a thread calls ThreadRegistry::getTID() it will allocate a free slot in 'usedTID[]'.
 * This tid wil be saved in a thread-local variable of the type ThreadCheckInCheckOut which
 * upon destruction of the thread will call the destructor of ThreadCheckInCheckOut and free the
 * corresponding slot to be used by a later thread.
 */
class ThreadRegistry {
private:
    static const bool kThreadPining = false;
    alignas(128) std::atomic<bool>      usedTID[REGISTRY_MAX_THREADS];   // Which TIDs are in use by threads
    alignas(128) std::atomic<int>       maxTid {-1};                     // Highest TID (+1) in use by threads

public:
    ThreadRegistry() {
        for (int it = 0; it < REGISTRY_MAX_THREADS; it++) {
            usedTID[it].store(false, std::memory_order_relaxed);
        }
    }

    // Progress condition: wait-free bounded (by the number of threads)
    int register_thread_new(void) {
        for (int tid = 0; tid < REGISTRY_MAX_THREADS; tid++) {
            if (usedTID[tid].load(std::memory_order_acquire)) continue;
            bool unused = false;
            if (!usedTID[tid].compare_exchange_strong(unused, true)) continue;
            // Increase the current maximum to cover our thread id
            int curMax = maxTid.load();
            while (curMax <= tid) {
                maxTid.compare_exchange_strong(curMax, tid+1);
                curMax = maxTid.load();
            }
            tl_tcico.tid = tid;
            // If thread pinning enabled, set it here
            if (kThreadPining) pinThread(tid);
            return tid;
        }
        printf("ERROR: Too many threads, registry can only hold %d threads\n", REGISTRY_MAX_THREADS);
        assert(false);
        return -1;
    }

    // Progress condition: wait-free population oblivious
    inline void deregister_thread(const int tid) {
        usedTID[tid].store(false, std::memory_order_release);
    }

    // Progress condition: wait-free population oblivious
    static inline uint64_t getMaxThreads(void) {
        return gThreadRegistry.maxTid.load(std::memory_order_acquire);
    }

    // Progress condition: wait-free bounded (by the number of threads)
    static inline int getTID(void) {
        int tid = tl_tcico.tid;
        if (tid != ThreadCheckInCheckOut::NOT_ASSIGNED) return tid;
        return gThreadRegistry.register_thread_new();
    }

    // Handle pinning
    inline void pinThread(const int tid) {
        cpu_set_t set;
        // tid 0 is usually 'main' so we pin it to first CPU too
        if (tid <= 20) {  // Our server (castor-1) has 10 cores => 20 HW threads and two CPUs
            CPU_ZERO(&set);
            sched_setaffinity(getpid(), sizeof(set), &set);
        }
    }
};


/*
 * EsLocoTu? is an Extremely Simple memory aLOCatOr Two
 *
 * It is based on intrusive singly-linked stacks (a free-list), one for each power of two size.
 * All blocks are powers of two, the smallest size enough to contain the desired user data plus the block header.
 * There is an array named 'freelists' where each entry is a pointer to the head of a stack for that respective block size.
 * Blocks are allocated in powers of 2 of words (64bit words).
 * Each block has an header with two words: the size of the node (in words), the pointer to the next node.
 * The minimum block size is 4 words, with 2 for the header and 2 for the user.
 * When there is no suitable block in the freelist, it will create a new block from the remaining pool.
 *
 * EsLoco was designed for usage in PTMs but it doesn't have to be used only for that.
 * EsLocoTu scales while EsLoco does not.
 * Average number of stores for an allocation is 1.
 * Average number of stores for a de-allocation is 2.
 *
 * Memory layout:
 * ---------------------------------------------------------------------------------------------------
 * | poolTop | gfreelists[0..50] | threadflists[0..REGISTRY_MAX_THREADS] | ... allocated objects ... |
 * ---------------------------------------------------------------------------------------------------
 */
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
        if (debugOn) printf("Starting EsLoco2 with poolAddr=%p and poolSize=%ld, up to %p\n", poolAddr, poolSize, poolAddr+poolSize);
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
        const int tid = ThreadRegistry::getTID();
        // Adjust size to nearest (highest) power of 2
        uint64_t bsize = highestBit(size + sizeof(block));
        uint64_t asize = (1ULL << bsize);
        if (debugOn) printf("malloc(%ld) requested,  block size exponent = %ld\n", size, bsize);
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
        const int tid = ThreadRegistry::getTID();
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


// Needed by our benchmarks and to prevent destructor from being called automatically on abort/retry
struct tmbase {
    ~tmbase() { }
};


struct ReadSet {
    struct ReadSetEntry {
        std::atomic<uint64_t>* mBeg;
        std::atomic<uint64_t>* mEnd;
    };

    // We pre-allocate a read-set with this many entries and if more are needed,
    // we grow them dynamically during the transaction and then delete them when
    // reset() is called on beginTx().
    static const int64_t MAX_ENTRIES = 16*1024;

    ReadSetEntry  entries[MAX_ENTRIES];
    uint64_t      size {0};          // Number of loads in the readSet for the current transaction
    ReadSet*      next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();   // Recursive call on the linked list of logs
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    inline bool validate(uint64_t rClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            for (std::atomic<uint64_t>* mutex = entries[i].mBeg; mutex <= entries[i].mEnd; mutex++) {
                uint64_t sl = mutex->load(std::memory_order_acquire);
                if (!isUnlockedOrLockedByMe(rClock, tid, sl)) return false;
            }
        }
        if (next != nullptr) return next->validate(rClock, tid); // Recursive call to validate()
        return true;
    }

    inline void add(std::atomic<uint64_t>* mBeg, std::atomic<uint64_t>* mEnd) {
        if (size != MAX_ENTRIES) {
            entries[size].mBeg = mBeg;
            entries[size].mEnd = mEnd;
            size++;
        } else {
            // The current node is full, therefore, search a vacant node or create a new one
            if (next == nullptr) next = new ReadSet();
            next->add(mBeg, mEnd);    // Recursive call to add()
        }
    }
};

// Size of an Optane block in 64bit words. Used to prevent "false sharing".
#define PM_PAD  (256/sizeof(uint64_t))

// The persistent metadata is a 'header'.
struct PMetadata {
    static const uint64_t   MAGIC_ID = 0x1337bab4;
    void*                   root {nullptr}; // Immutable once assigned
    uint64_t                id {0};
    uint64_t                padding[8-2];

    volatile s128t          versionData;    
    uint64_t                padding2[8-2];

    HashSlot                persistentHashTable[BUFFER_HASH_TABLE_SIZE];

    PMetadata() {
    }
};

// Size of presistent memory region without metadata
static uint64_t PM_SIZE = (PM_REGION_SIZE-sizeof(PMetadata));
// Size of volatile memory region (VR)
static uint64_t VR_SIZE = (PM_SIZE)*24ULL/64;
// End address of volatile memory region (VR)
static uint8_t* VREGION_END = VREGION_ADDR+VR_SIZE;
// start of PM without metadata
static uint8_t* PM_REGION_START = ((uint8_t*)PMREGION_ADDR+sizeof(PMetadata));
// Get the address of the PMCacheLine in PM
#define VR_2_PCL(_addr)  ((((size_t)_addr - (size_t)VREGION_ADDR)/24)*64 + (size_t)PM_REGION_START)
// Convert an address of a volatile byte into a PM byte of 'main'.
#define VR_2_PM(_addr)   ((((size_t)_addr - (size_t)VREGION_ADDR)/24)*64 + ( ((size_t)_addr-(size_t)VREGION_ADDR)%24 ) + (size_t)PM_REGION_START)
// Convert from a generic PM address to a VR address
#define PM_2_VR(_addr)   ((((size_t)_addr - (size_t)PM_REGION_START)/64)*24 + ( ((size_t)_addr-(size_t)PM_REGION_START)%64 ) + (size_t)VREGION_ADDR)
// Convert an address of a volatile byte into a PM byte of 'main'.
#define PCL_2_VCL(_addrpcl)   (((((size_t)_addrpcl)-(size_t)PM_REGION_START)/64)*24 + (size_t)VREGION_ADDR)


// Address of Persistent Metadata.
// This relies on PMetadata being the first thing in the persistent region.
static PMetadata* const pmd = (PMetadata*)PMREGION_ADDR;

// We put the array of locks outside the PTM because we want to access it from stand-alone static methods
extern std::atomic<uint64_t> *gHashLock;

//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//                          Buffer stuff
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
#define BCAS __sync_bool_compare_and_swap
#define VCAS __sync_val_compare_and_swap

#define LOCK_HASH_SLOT(vNum) ((vNum << 1) | 1)
#define UNLOCK_HASH_SLOT(vNum) (vNum & (~1))
#define VOL_HASH_VNUM_TO_UNMARKED_VNUM(hashVNum) (hashVNum >> 1)
#define VNUM_TO_UNLOCKED_HASH_VNUM(hashVNum) (hashVNum << 1)

HashSlot                hashTableBuffer[BUFFER_HASH_TABLE_SIZE];

volatile uint64_t       claimedChunks = 0;
volatile uint64_t       pChunks = 0;
volatile uint64_t       wbChunks = TOTAL_CHUNKS;

volatile uint64_t       committerCounter = 0;
volatile uint64_t       tableSizeAndCTSLF = 0;

dcssProvider<void* /* unused */> * prov;

//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------
//--------------------------------------------------------------------


// Volatile log (write-set)
struct AppendLog {
    // We pre-allocate a write-set with this many entries and if more are needed,
    // we grow them dynamically during the transaction and then delete them when
    // reset() is called on beginTx().
    static const int64_t MAX_ENTRIES = 8*1024;

    struct LogEntry {
        intptr_t*  vraddr;
        intptr_t   val;
        uint32_t  length;
    };

    int64_t       size {0};
    LogEntry      entries[MAX_ENTRIES];
    AppendLog*    next {nullptr};

    inline void reset() {
        if (next != nullptr) {
            next->reset();   // Recursive call on the linked list of logs
            delete next;
            next = nullptr;
        }
        size = 0;
    }

    // Adds VR ranges to the log
    inline void add(intptr_t* vraddr, intptr_t val, uint32_t length) {
        if (size != MAX_ENTRIES) {
            entries[size].vraddr = vraddr;
            entries[size].val = val;
            entries[size].length = length;
            size++;
        } else {
            // The current node is full, therefore, search a vacant node or create a new one
            if (next == nullptr) next = new AppendLog();
            next->add(vraddr, val, length); // recursive call to add()
        }
    }


    // Helper function for rollbackVR()
    inline bool rangeIsLockedByMe(void* vraddr, uint32_t length, uint64_t tid) {
        std::atomic<uint64_t>* mBeg  = &gHashLock[hidx(VR_2_PCL(vraddr))];
        std::atomic<uint64_t>* mEnd  = &gHashLock[hidx(VR_2_PCL(((uint8_t*)vraddr) + length-1))];
        for (std::atomic<uint64_t>* mutex = mBeg; mutex <= mEnd; mutex++) {
            uint64_t sl = mutex->load(std::memory_order_acquire);
            if (sl != (LOCKED | tid)) return false;
        }
        return true;
    }

    // Rollback modifications on VR: called from abortTx() to revert changes.
    void rollbackVR(uint64_t tid) {
        if (size == 0) return;
        if (next == nullptr) {
            for (int64_t i = 0; i < size-1; i++) {
                *entries[i].vraddr = entries[i].val;                
            }
            // The last entry in the log is special: we don't revert it unless
            // we have all the locks protecting it. The reason being that maybe the
            // abort was triggered due to failed lock acquisition and therefore,
            // reverting it would be incorrect (is already locked by another tx).
            if (rangeIsLockedByMe(entries[size-1].vraddr, entries[size-1].length, tid)) {
                *entries[size-1].vraddr = entries[size-1].val;
            }
        }
        if (next != nullptr) {
            for (int64_t i = 0; i < size; i++) {
                *entries[i].vraddr = entries[i].val;
            }
            next->rollbackVR(tid); // Recursive call to rollbackVR()
        }
    }

    // Unlock all the locks acquired by this thread
    inline void unlock(uint64_t nextClock, uint64_t tid) {
        for (uint64_t i = 0; i < size; i++) {
            void* vraddr = entries[i].vraddr;
            int32_t length = entries[i].length;
            // Handle ranges correctly
            std::atomic<uint64_t>* mBeg  = &gHashLock[hidx(VR_2_PCL(vraddr))];
            std::atomic<uint64_t>* mEnd  = &gHashLock[hidx(VR_2_PCL(((uint8_t*)vraddr) + length-1))];
            for (std::atomic<uint64_t>* mutex = mBeg; mutex <= mEnd; mutex++) {
                if (mutex->load(std::memory_order_relaxed) == (LOCKED | tid)) {
                    mutex->store(nextClock, std::memory_order_release);
                }
            }
        }
        if (next != nullptr) next->unlock(nextClock, tid);  // Recursive cal to unlock()
    }


    //------------------------------------------------------------------------
    //------------------------------------------------------------------------
    //------------------------------------------------------------------------
    s128t incAndSetState (s128t vData, int state) {
        uint64_t newV = GET_UNMARKED_VERSION_NUM(vData) + 1;
        newV = (newV << MARK_BITS_COUNT) | state;

        uint32_t newChksToPersist = GET_CHUNKS_TO_PERSIST(vData);
        if (state == hashTableBufferStates::WRITE_MODE_DURABLE) {
            newChksToPersist = newChksToPersist + (TOTAL_CHUNKS * 3);
        } 
        s128t newVData;
        newVData.low = newV;
        newVData.hi = newChksToPersist;
        
        s128t ret;
        ret.hi = pmd->versionData.hi;
        ret.low = pmd->versionData.low;
        if (DWCAS_s128t(&pmd->versionData, vData, newVData)) {        
            ret = newVData;
        }    
        PWB(&pmd->versionData);
        return ret;
    }

    void insertWriteSet(uint64_t unlockedHashVNum) {         
        for (int i = 0; i < this->size; i++) {            
            volatile intptr_t* addr = entries[i].vraddr;
            intptr_t val = entries[i].val;

            uint64_t startIndex = xorshift64((uint64_t)addr) % BUFFER_HASH_TABLE_SIZE;
            uint64_t index = startIndex;
            
            uint64_t slotVer;            
            while (true) {   
    PROBING_RETRY:  
                slotVer = hashTableBuffer[index].versionNum;                     
                while (hashTableBuffer[index].addr != addr && slotVer == unlockedHashVNum) {
                    index = (index + 1) % BUFFER_HASH_TABLE_SIZE;
                    slotVer = hashTableBuffer[index].versionNum; 

                    //assuming our thresholds are large enough we should never be able to
                    // roll over (loop completely around the table while probing)
                    if (index == startIndex) {                    
                        printf("------------\n");
                        printf("------------\n");
                        printf("ERROR ROLLED OVER\n");
                        printf("------------\n");
                        printf("------------\n");
                        exit(-1);
                    }                     
                }            

                //slot is empty
                if (slotVer < unlockedHashVNum) {
                    if (!BCAS(&hashTableBuffer[index].versionNum, slotVer, LOCK_HASH_SLOT(unlockedHashVNum))) {                    
                        continue;
                    } 
                    break; 
                }
                
                //slot is occupied but it contains our address
                if (hashTableBuffer[index].addr == addr && slotVer == unlockedHashVNum) {
                    while (!BCAS(&hashTableBuffer[index].versionNum, slotVer, LOCK_HASH_SLOT(unlockedHashVNum))) {                                                        
                        //Since the reads to table[idx].addr and table[idx].vnum are not done together as an atomic op
                        //   it is possible (but unlikely) that this condition could have changed between the reads
                        if (hashTableBuffer[index].addr != addr || hashTableBuffer[index].versionNum != unlockedHashVNum) {
                            goto PROBING_RETRY;
                        } 
                    }   
                    break; 
                }
            } 


            hashTableBuffer[index].addr = addr;
            hashTableBuffer[index].val = val;
            hashTableBuffer[index].versionNum = unlockedHashVNum;
        }      

        if (next) {
            next->insertWriteSet(unlockedHashVNum);
        }  
    }

    //Copy the hash table to persistent memory
    void copyChunkToPmem(long tid, uint32_t chunk, uint64_t unMarkedVNum) {
        chunk = chunk % TOTAL_CHUNKS;
        uint32_t slotIndex = CHUNK_SIZE * chunk;
        uint32_t chunkEnd = slotIndex + CHUNK_SIZE;

        // for each (addr, val) in chunk - insert (addr, val) into pmem hash table using DWACS        
        HashSlot currSlot;
        HashSlot oldVal;
        for (; slotIndex < chunkEnd; slotIndex++) {        
            currSlot = hashTableBuffer[slotIndex]; 

            if (currSlot.versionNum <= unMarkedVNum - 3) {
                continue;
            }
            
            //NOTE:: this lock is essentially fake and only necessary for the scenario where we crash
            //      and the entry has only been partially written        
            pmd->persistentHashTable[slotIndex].versionNum = VOL_HASH_VNUM_TO_UNMARKED_VNUM(currSlot.versionNum);                
            pmd->persistentHashTable[slotIndex].addr = currSlot.addr;        
            pmd->persistentHashTable[slotIndex].val = currSlot.val;        

            //no need to flush each field since the slot is padded
            PWB(&pmd->persistentHashTable[slotIndex]);
        }
    }

    //In persistent memory apply the values of addresses that are in the hash table buffer
    void applyWrites(long tid, uint32_t chunk, uint64_t unMarkedVNum) {
        chunk = chunk % TOTAL_CHUNKS;
        uint32_t slotIndex = CHUNK_SIZE * chunk;
        uint32_t chunkEnd = slotIndex + CHUNK_SIZE;
        
        // for each (addr, val) in chunk - insert (addr, val) into pmem hash table using DWACS        
        for (; slotIndex < chunkEnd; slotIndex++) {
            //we can use the volatile hash table here since in write back mode,
            //the hash table is fixed and cannot be changed until we finish the write back
            uint64_t slotVNum = VOL_HASH_VNUM_TO_UNMARKED_VNUM(hashTableBuffer[slotIndex].versionNum);
            if (slotVNum <= unMarkedVNum - 3) {
                continue;
            }
            
            volatile intptr_t* addr = hashTableBuffer[slotIndex].addr;
            intptr_t val = hashTableBuffer[slotIndex].val;

            uintptr_t addrOffset = (uintptr_t)addr - (uintptr_t)VREGION_ADDR;
            uintptr_t pmemAddr = (uintptr_t)PM_REGION_START + addrOffset;
            (*(intptr_t*)pmemAddr) = val;
        }
    }

    void helpWriteBackTransactions(long tid, s128t vData, uint64_t ctslfAndSize) {
        uint64_t cc = claimedChunks;
        uint64_t chunksToWriteBack = GET_CHUNKS_TO_WRITEBACK(vData);
        while (cc < chunksToWriteBack) {
            //we could possibly replace this with a FAA however we dont want it to 
            // be able to go past chunksToWriteBack and FAA would not prevent this
            uint64_t myChunk = VCAS(&claimedChunks, cc, cc+1);
            if (myChunk == cc) {
                applyWrites(tid, myChunk, GET_UNMARKED_VERSION_NUM(vData));            
                //We add 2 to wbChunks and 1 to pChunks because we do not
                //  reset or adjust the claimedChunks and we need 
                //  pChunks to equal claimedChunks on the next iteration
                //  and we dont want to reset pChunks and wbChunks
                FAA(&wbChunks, 2);
                FAA(&pChunks, 1);
            }
            cc = claimedChunks;
        }

        while (wbChunks < chunksToWriteBack && pChunks < chunksToWriteBack) {    
            // busy wait
        }

        uint64_t newCtslfAndSize = 0;
    retry_DCSS_CTSLF_SIZE_RESET:    
        int result = prov->dcssVal(tid, (casword_t*)&pmd->versionData.low, vData.low, 
            (casword_t*)&tableSizeAndCTSLF, (casword_t)ctslfAndSize, (casword_t)(newCtslfAndSize)).status;        
        if (result != DCSS_SUCCESS) {
            if (result == DCSS_FAILED_ADDR1) {
                return;
            }                         
            ctslfAndSize = prov->readVal(tid, (casword_t*)&tableSizeAndCTSLF);         
            goto retry_DCSS_CTSLF_SIZE_RESET;            
        }
        s128t newVData = incAndSetState(vData, hashTableBufferStates::WRITE_MODE_DURABLE);
    }

    void helpPersist(long tid, s128t vData, uint64_t ctslfAndSize) {
        while (committerCounter > 0) { 
            //busy wait
        }

        uint64_t cc = claimedChunks;
        uint64_t chunksToPersist = GET_CHUNKS_TO_PERSIST(vData);
        while (cc < chunksToPersist) {                
            uint64_t myChunk = VCAS(&claimedChunks, cc, cc+1);
            if (myChunk == cc) {
                copyChunkToPmem(tid, myChunk, GET_UNMARKED_VERSION_NUM(vData));
                FAA(&pChunks, 1);            
            }        
            cc = claimedChunks;
        }

        while (pChunks < chunksToPersist) {    
            // busy wait
        }

        s128t newVData = incAndSetState(vData, WRITE_BACK_MODE_DURABLE);
        PWB(&pmd->versionData);
        if (getState(newVData) == WRITE_BACK_MODE_DURABLE) {
            helpWriteBackTransactions(tid, newVData, ctslfAndSize);
            return;
        }
    }


    void initiateFlush(long tid, s128t vData, uint64_t ctslfAndSize) {
        s128t newVData = incAndSetState(vData, PERSIST_MODE_DURABLE);
        PWB(&pmd->versionData);
        if (getState(newVData.low) == PERSIST_MODE_DURABLE) {
            helpPersist(tid, newVData, ctslfAndSize);
            return;
        }
    }





    void bufferWriteSet(){
        if (size == 0) {
            return;
        }

        const int tid = ThreadRegistry::getTID();
        s128t vData;

        while (true) {
            vData.hi = pmd->versionData.hi;
            vData.low = pmd->versionData.low;

            PWB(&pmd->versionData);                             

            uint64_t ctslfAndSize = prov->readVal(tid, (casword_t*)&tableSizeAndCTSLF); 

            int state = getState(vData);    
            if (state == PERSIST_MODE_DURABLE) {
                helpPersist(tid, vData, ctslfAndSize);
                continue;
            }
            
            if (state == WRITE_BACK_MODE_DURABLE) {            
                helpWriteBackTransactions(tid, vData, ctslfAndSize);
                continue;
            }

            if (state != WRITE_MODE_DURABLE) {
                continue;
            }

            //version number we read is in write mode
            //try to claim the right to add our write set                 
            //no need for DCSS or DWCAS here since we will always do a DCSS or DWCAS where
            //one addr is the version number as the next step        
            FAA(&committerCounter, 1);        

            //we have the right to add our write set
            //need to check if too many transactions have committed
            
            uint32_t tblSize = (uint32_t)ctslfAndSize;
            uint32_t* ctslfPtr = (uint32_t*)(&ctslfAndSize);        
            ctslfPtr++;
            uint32_t ctslf = *(ctslfPtr);


            uint64_t newCtslfAndSize = ctslfAndSize;
retry_DCSS_CTSLF_SIZE:
            if (ctslf + 1 >= MAX_COMMITED_TRANSACTIONS_BEFORE_FLUSH) {
                FAA(&committerCounter, -1); 
                initiateFlush(tid, vData, ctslfAndSize);
                continue;
            }
            if (tblSize + this->size > MAX_TABLE_SIZE_BEFORE_FLUSH){
    
                FAA(&committerCounter, -1); 
                initiateFlush(tid, vData, ctslfAndSize);    
                continue;
            }        

            newCtslfAndSize = (((uint64_t)(ctslf + 1)) << 32) | (tblSize + this->size);
            
            int result = prov->dcssVal(tid, (casword_t*)&pmd->versionData.low, vData.low, 
            (casword_t*)&tableSizeAndCTSLF, (casword_t)ctslfAndSize, (casword_t)(newCtslfAndSize)).status;
            if (result != DCSS_SUCCESS) {
                if (result == DCSS_FAILED_ADDR1) {
                    FAA(&committerCounter, -1); 
                    continue;
                }            
                ctslfAndSize = prov->readVal(tid, (casword_t*)&tableSizeAndCTSLF);  
                tblSize = (uint32_t)ctslfAndSize;
                ctslfPtr = (uint32_t*)(&ctslfAndSize);        
                ctslfPtr++;
                ctslf = *(ctslfPtr);            
                goto retry_DCSS_CTSLF_SIZE;            
            }
    

            //We can now add all of our writes to the table
            insertWriteSet(VNUM_TO_UNLOCKED_HASH_VNUM(GET_UNMARKED_VERSION_NUM(vData)));

            //we are no longer a committer
            FAA(&committerCounter, -1);
            return;
        }
    }
};


// Thread-local data
struct OpData {
    std::jmp_buf env;
    uint64_t     attempt {0};
    uint64_t     tid;
    uint64_t     rClock {0};
    int          tx_type {TX_IS_NONE}; // This is used by persist::load() to figure out if it needs to save a load on the read-set or not
    ReadSet      readSet;              // The (volatile) read set
    AppendLog    writeSet {};          // Write-set: The append-only log of modified persist<T>
    uint64_t     myrand;
    uint64_t     numAborts {0};
    uint64_t     numCommits {0};
    uint64_t     padding[17];
};

extern std::atomic<uint64_t> gClock;

[[noreturn]]
extern void abortTx(OpData* myd);

// This is used by addToLog() to know which OpData instance to use for the current transaction
extern thread_local OpData* tl_opdata;

// Helper function to lock an entire range. Used by pstore() and some of the string utils
inline static void logLockRange(intptr_t* vraddr, intptr_t val, int32_t length) {
    OpData* const myd = tl_opdata;
    // We must log _before_ locking because, in case of an abort half-way through
    // the lock acquisitions on a range, we want to revert those acquisitions and
    // for that, we need to have that range kept already in the log.
    myd->writeSet.add(vraddr, val, length);
    std::atomic<uint64_t>* mBeg  = &gHashLock[hidx(VR_2_PCL(vraddr))];
    std::atomic<uint64_t>* mEnd  = &gHashLock[hidx(VR_2_PCL(((uint8_t*)vraddr) + length-1))];
    for (std::atomic<uint64_t>* mutex = mBeg; mutex <= mEnd; mutex++) {
        uint64_t sl = mutex->load(std::memory_order_acquire);
        if (!isUnlockedOrLockedByMe(myd->rClock, myd->tid, sl)) abortTx(myd);
        if (isUnlocked(sl) && !mutex->compare_exchange_strong(sl, LOCKED | myd->tid)) abortTx(myd);
    }
}

// Same as checkRange(), but handles a range and is not inlined (slow-path)
static void checkRangeSlow(OpData* const myd, std::atomic<uint64_t>* mBeg, std::atomic<uint64_t>* mEnd) {
    // Handle wrap-around of lock ranges
    if (mEnd < mBeg) {
        if (myd->tx_type == TX_IS_UPDATE) myd->readSet.add(&gHashLock[0], mBeg);
        for (std::atomic<uint64_t>* mutex = &gHashLock[0]; mutex <= mBeg; mutex++) {
            uint64_t sl = mutex->load(std::memory_order_acquire);
            if (!isUnlockedOrLockedByMe(myd->rClock, myd->tid, sl)) abortTx(myd);
        }
        mBeg = mEnd;
        mEnd = &gHashLock[NUM_LOCKS-1];
    }
    // When in a write tx, loads must be added to the read-set
    if (myd->tx_type == TX_IS_UPDATE) myd->readSet.add(mBeg, mEnd);
    for (std::atomic<uint64_t>* mutex = mBeg; mutex <= mEnd; mutex++) {
        uint64_t sl = mutex->load(std::memory_order_acquire);
        if (!isUnlockedOrLockedByMe(myd->rClock, myd->tid, sl)) abortTx(myd);
    }
}

// Helper function to check an entire (VR) range. Used by pload() and string utils
// Make sure to issue a "asm volatile ("" : : : "memory")" before calling this.
inline static void checkRange(OpData* const myd, void* vraddr, std::size_t length) {
    if (myd == nullptr || vraddr < VREGION_ADDR || vraddr >= VREGION_END) return;
    std::atomic<uint64_t>* mBeg  = &gHashLock[hidx(VR_2_PCL(vraddr))];
    std::atomic<uint64_t>* mEnd  = &gHashLock[hidx(VR_2_PCL(((uint8_t*)vraddr) + length-1))];
    if (mBeg == mEnd) {
        // Fast path for single-lock checks
        if (myd->tx_type == TX_IS_UPDATE) myd->readSet.add(mBeg, mEnd);
        uint64_t sl = mBeg->load(std::memory_order_acquire);
        if (!isUnlockedOrLockedByMe(myd->rClock, myd->tid, sl)) abortTx(myd);
    } else {
        // Slow path for multi-lock checks
        checkRangeSlow(myd, mBeg, mEnd);
    }
}

// T is typically a pointer to a node, but it can be integers or other stuff, as long as it fits in 64 bits
template<typename T> struct persist {
    T vrmain;

    persist() { }

    persist(T initVal) { pstore(initVal); }

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
    persist<T>& operator+= (const T& rhs) { pstore(pload() + rhs); return *this; }
    persist<T>& operator-= (const T& rhs) { pstore(pload() - rhs); return *this; }

    // Equals operator
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator == (const persist<Y> &rhs) { return pload() == rhs; }
    // Difference operator: first downcast to T and then compare
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator != (const persist<Y> &rhs) { return pload() != rhs; }
    // Relational operators
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator < (const persist<Y> &rhs) { return pload() < rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator > (const persist<Y> &rhs) { return pload() > rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator <= (const persist<Y> &rhs) { return pload() <= rhs; }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator >= (const persist<Y> &rhs) { return pload() >= rhs; }

    // Operator arrow ->
    T operator->() { return pload(); }

    // Copy constructor
    persist<T>(const persist<T>& other) { pstore(other.pload()); }

    // Assignment operator from a persist<T>
    persist<T>& operator=(const persist<T>& other) {
        pstore(other.pload());
        return *this;
    }

    // Assignment operator from a value
    persist<T>& operator=(T value) {
        pstore(value);
        return *this;
    }

    // Operator &
    T* operator&() {
        return (T*)this;
    }

    // Store interposing: acquire the lock and write in 'main'
    inline void pstore(T newVal) {
        intptr_t* vraddr = (intptr_t*)&vrmain;
        intptr_t oldVal = (intptr_t)vrmain;
        OpData* const myd = tl_opdata;
        // We don't acquire locks for data outside PM (that woud make more overhead on the logging system)
        if (myd != nullptr && (uint8_t*)vraddr >= VREGION_ADDR && (uint8_t*)vraddr < VREGION_END) {
            // Logs stores and acquires locks, or aborts
            logLockRange(vraddr, oldVal, sizeof(T));
        }
        vrmain = newVal;
    }

    // This is similar to an undo-log load interposing: do a single post-check
    inline T pload() const {
        T lval = vrmain;
        asm volatile ("" : : : "memory");
        checkRange(tl_opdata, (void*)&vrmain, sizeof(T)); // Aborts if lock is inconsistent or taken
        return lval;
    }
};


class Trinity;
extern Trinity gTrinity;


class Trinity {
private:
    // Padding on x86 should be on 2 cache lines
    static const int                       CLPAD = 128/sizeof(uintptr_t);
    bool                                   reuseRegion {false};                 // used by the constructor and initialization
    int                                    pfd {-1};
    int                                    vfd {-1};
    alignas(128) OpData                   *opDesc;
    EsLoco2<persist>                       esloco {};

public:
    struct tmbase : public trinityvrtl2::tmbase { };

    Trinity() {        
    	assert(sizeof(PMetadata)%64 == 0);
    	gHashLock = new std::atomic<uint64_t>[NUM_LOCKS];
    	for (int i=0; i < NUM_LOCKS; i++) gHashLock[i].store(0, std::memory_order_relaxed);
        opDesc = new OpData[REGISTRY_MAX_THREADS];
        for (uint64_t it=0; it < REGISTRY_MAX_THREADS; it++) {
            opDesc[it].tid = it;
            opDesc[it].myrand = (it+1)*12345678901234567ULL;
        }
        mapPersistentRegion(PM_FILE_NAME, (uint8_t*)PMREGION_ADDR, PM_REGION_SIZE);
        // The size of the volatile region is 24/64 the size of the PM region
        mapVolatileRegion(VFILE_NAME, VREGION_ADDR, VR_SIZE);

       
    }

    ~Trinity() {
        uint64_t totalAborts = 0;
        uint64_t totalCommits = 0;
        for (int it=0; it < REGISTRY_MAX_THREADS; it++) {
            totalAborts += opDesc[it].numAborts;
            totalCommits += opDesc[it].numCommits;
        }
        printf("totalAborts=%ld  totalCommits=%ld  abortRatio=%.1f%%   usedPM=%ld MB\n",
                totalAborts, totalCommits, 100.*totalAborts/(1+totalCommits), (esloco.getUsedSize()*64)/(24*1024*1024));
        delete[] opDesc;
        delete[] gHashLock;
    }

    void initBufferData(){
        prov = new dcssProvider<void* /* unused */>(MAX_THREADS_POW2);    
        pmd->versionData.low = (3 << (MARK_BITS_COUNT)) | WRITE_MODE_DURABLE;      
        pmd->versionData.hi = TOTAL_CHUNKS;
    }

    static std::string className() { return "TrinityVR-TL2"; }

    void mapPersistentRegion(const char* filename, uint8_t* regionAddr, const uint64_t regionSize) {
        // Check that the header with the logs leaves at least half the memory available to the user
        if (sizeof(PMetadata) > regionSize/2) {
            printf("ERROR: the size of the header in persistent memory is so large that it takes more than half the whole persistent memory\n");
            printf("Please reduce some of the settings in TrinityVRTL2.hpp and try again\n");
            assert(false);
        }
        // Check if the file already exists or not
        struct stat buf;
        if (stat(filename, &buf) == 0) {
            // File exists
            // pfd = open(filename, O_RDWR|O_CREAT, 0755);
            // if (pfd < 0) {
            //     perror("open() error");
            //     assert(pfd >= 0);
            // }            
            // // reuseRegion = true;        
            printf("Removing old PMEM file.\n");            
            remove(filename);
            pfd = open(filename, O_RDWR|O_CREAT, 0755);
            if (pfd < 0) {
                perror("open() error");
                assert(pfd >= 0);
            }
            if (lseek(pfd, regionSize-1, SEEK_SET) == -1) {
                perror("lseek() error");
            }
            if (write(pfd, "", 1) == -1) {
                perror("write() error");
            }
        } else {
            // File doesn't exist
            pfd = open(filename, O_RDWR|O_CREAT, 0755);
            if (pfd < 0) {
                perror("open() error");
                assert(pfd >= 0);
            }
            if (lseek(pfd, regionSize-1, SEEK_SET) == -1) {
                perror("lseek() error");
            }
            if (write(pfd, "", 1) == -1) {
                perror("write() error");
            }
        }
        // Try one time to mmap() with DAX. If it fails due to MAP_FAILED, then retry without DAX.
        // We may fail because the address is not available. Retry at most 4 times, then give up.
        uint64_t dax_flag = MAP_SYNC;
        void* got_addr = NULL;
        for (int i = 0; i < 5; i++) {
            got_addr = mmap(regionAddr, regionSize, (PROT_READ | PROT_WRITE), MAP_SHARED_VALIDATE | dax_flag, pfd, 0);
            if (got_addr == regionAddr) {                
                break;
            }
            if (got_addr == MAP_FAILED) {
                perror("ERROR: mmap() returned MAP_FAILED !!! ");
                assert(false);
                exit(-1);
            } else {
                // mmap() worked but in wrong address. Let's unmap() and try again.
                if (i != 4) {
                    munmap(got_addr, regionSize);
                    // Sleep for a second before retrying the mmap()
                    usleep(1000);
                }
            }
        }
        if (got_addr == MAP_FAILED) {
            perror("ERROR: mmap() returned MAP_FAILED !!! ");
            assert(false);
            exit(-1);
        }
        if (got_addr != regionAddr) {
            // printf("Mmap did not map to desired address after all tries. Exiting\n");                
            // exit(-1);
            printf("Mmap did not map to desired address after all tries.\n");                
            regionAddr = (uint8_t*)got_addr;
            PMREGION_ADDR = (uint8_t*)got_addr;
        }
    }

    // Maps the volatile memory region
    void mapVolatileRegion(const char* filename, uint8_t* regionAddr, const uint64_t regionSize) {        
        reuseRegion = false;

        // Check if the file already exists or not
        struct stat buf;
        if (stat(filename, &buf) == 0) {
            // File exists
            vfd = open(filename, O_RDWR|O_CREAT, 0755);
            if (vfd < 0) {
                perror("open() error");
                assert(vfd >= 0);
            }
            printf("Removing old volatile memory file.\n");
            close(vfd);
            remove(filename);
            vfd = open(filename, O_RDWR|O_CREAT, 0755);
            if (vfd < 0) {
                perror("open() error");
                assert(vfd >= 0);
            }
            if (lseek(vfd, regionSize-1, SEEK_SET) == -1) {
                perror("lseek() error");
            }
            if (write(vfd, "", 1) == -1) {
                perror("write() error");
            }
        } else {
            // File doesn't exist
            // Lets check for enough storage space:
            /*
            std::filesystem::space_info sp = std::filesystem::space(filename);
            if (sp.available < regionSize) {
                printf("ERROR: %ld MB isn't enough space to create VR file [%s] with %ld MB\n",
                        sp.available/(1024*1024), filename, regionSize/(1024*1024));
            }
            */
            vfd = open(filename, O_RDWR|O_CREAT, 0755);
            if (vfd < 0) {
                perror("open() error");
                assert(vfd >= 0);
            }
            if (lseek(vfd, regionSize-1, SEEK_SET) == -1) {
                perror("lseek() error");
            }
            if (write(vfd, "", 1) == -1) {
                perror("write() error");
            }
        }
        // mmap() volatile memory range 'main'
        uint8_t* got_addr = nullptr;
        for (int i = 0; i < 5; i++){
            got_addr = (uint8_t *)mmap(regionAddr, regionSize, (PROT_READ | PROT_WRITE), MAP_SHARED, vfd, 0);            
            if (got_addr == regionAddr) {
                break;
            }
            if (got_addr == MAP_FAILED) {
                perror("ERROR: mmap() returned MAP_FAILED !!! ");
                assert(false);
                exit(-1);
            }
            if (got_addr != regionAddr) {
                // printf("Volatile Region mapped at addr = %p instead of %p\n", got_addr, regionAddr);                
                if (i != 4) {
                    munmap(got_addr, regionSize);
                    // Sleep for a second before retrying the mmap()
                    usleep(1000);
                }
            }
        }
        if (got_addr != regionAddr) {
            // printf("Mmap did not map to desired address after all tries. Exiting\n");                
            // exit(-1);
            printf("Mmap did not map to desired address after all tries.\n");                
            VREGION_ADDR = got_addr;
            regionAddr = got_addr;
        }


        
        //GUY:: recovery is disabled make new persistent header
        
        // Call PMedata constructor
        new (pmd) PMetadata();
        // We reset the entire memory region because the 'seq' have to be zero.
        // This operation can be be super SLOOWWW on a large PM region.
        // Think of this memset() as "formatting' the PM.
        // std::memset(PM_REGION_START, 0, PM_SIZE);
        std::memset(PM_REGION_START, 0, sizeof(PMetadata));

        initBufferData();

        // Reset VR
        // std::memset(regionAddr, 0, regionSize);
        updateTx([&] () {
            // Initialize the allocator and allocate an array for the root pointers
            esloco.init(regionAddr, regionSize, true);
            pmd->root = esloco.malloc(sizeof(persist<void*>)*MAX_ROOT_POINTERS);
        });
        PWB(&pmd->root);
        PFENCE();
        pmd->id = PMetadata::MAGIC_ID;
        PWB(&pmd->id);
        PFENCE();
        // From this point on, a crash will trigger the "reuseRegion" code path
        
    }

    inline void beginTx(OpData* myd, const int tid) {
        // Clear the logs of the previous transaction
        myd->writeSet.reset();
        myd->readSet.reset();
        myd->rClock = gClock.load(); // This is GVRead()
    }

    // This PTM does eager locking, which means that by now all locks have been acquired.
    inline bool endTx(OpData* myd, const int tid) {
        // Check if this is a read-only transaction and if so, commit immediately
        if (myd->writeSet.size == 0) {
            myd->attempt = 0;
            return true;
        }
        // This fence is needed by undo log to prevent re-ordering with the last store
        // and the reading of the gClock.
        std::atomic_thread_fence(std::memory_order_seq_cst);
        // Validate the read-set
        if (!myd->readSet.validate(myd->rClock, tid)) abortTx(myd);
        // Tx is now committed and holding all the locks. Start the durable commit
#ifndef DISABLE_PERSISTENT_MEMORY_USE        
        // myd->writeSet.bufferWriteSet();
#endif

        // The FAA is 'hijacked' to act as a persistence fence
        uint64_t nextClock = gClock.fetch_add(1)+1;       

        // Unlock and set new sequence on the locks
        myd->writeSet.unlock(nextClock, tid);
        myd->numCommits++;
        myd->attempt = 0;
        return true;
    }

    template<typename F> void transaction(F&& func, int txType=TX_IS_UPDATE) {
        if (tl_opdata != nullptr) {
            func();
            return ;
        }
        const int tid = ThreadRegistry::getTID();
        OpData* myd = &opDesc[tid];
        tl_opdata = myd;
        myd->tx_type = txType;
        setjmp(myd->env);
        myd->attempt++;
        backoff(myd, myd->attempt);
        beginTx(myd, tid);
        func();
        endTx(myd, tid);
        tl_opdata = nullptr;
    }

    // It's silly that these have to be static, but we need them for the (SPS) benchmarks due to templatization
    //template<typename R, typename F> static R updateTx(F&& func) { return gTrinity.transaction<R>(func, TX_IS_UPDATE); }
    //template<typename R, typename F> static R readTx(F&& func) { return gTrinity.transaction<R>(func, TX_IS_READ); }
    template<typename F> static void updateTx(F&& func) { gTrinity.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTx(F&& func) { gTrinity.transaction(func, TX_IS_READ); }
    // There are no sequential durable transactions in TL2 but we "emulate it" with a concurrent+durable tx
    template<typename F> static void updateTxSeq(F&& func) { gTrinity.transaction(func, TX_IS_UPDATE); }
    template<typename F> static void readTxSeq(F&& func) { gTrinity.transaction(func, TX_IS_READ); }


    // Random number generator used by the backoff scheme
    inline uint64_t marsagliaXORV(uint64_t x) {
        if (x == 0) x = 1;
        x ^= x << 6;
        x ^= x >> 21;
        x ^= x << 7;
        return x;
    }

    // Backoff for a random amount of steps in the range [16, 16*attempt]. Inspired by TL2
    inline void backoff(OpData* myopd, uint64_t attempt) {
        if (attempt < 3) return;
        if (attempt == 10000) printf("Ooops, looks like we're stuck attempt=%ld\n", attempt);
        myopd->myrand = marsagliaXORV(myopd->myrand);
        uint64_t stall = (myopd->myrand & attempt) + 1;
        stall *= 16;
        std::atomic<uint64_t> iter {0};
        while (iter.load() < stall) iter.fetch_add(1);
        if (stall > 1000) std::this_thread::yield();
    }

    template<typename R,class F> inline static R readTx(F&& func) {
        gTrinity.transaction([&]() {func();}, TX_IS_READ);
        return R{};
    }
    template<typename R,class F> inline static R updateTx(F&& func) {
        gTrinity.transaction([&]() {func();}, TX_IS_UPDATE);
        return R{};
    }

    template <typename T, typename... Args> static T* tmNew(Args&&... args) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        void* ptr = gTrinity.esloco.malloc(sizeof(T));
        // If we get nullptr then we've ran out of PM space
        assert(ptr != nullptr);
        // If there is an abort during the 'new placement', the transactions rolls
        // back and will automatically revert the changes in the allocator metadata.
        new (ptr) T(std::forward<Args>(args)...);  // new placement
        return (T*)ptr;
    }

    template<typename T> static void tmDelete(T* obj) {
        if (obj == nullptr) return;
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not de-allocate outside a transaction\n");
            return;
        }
        obj->~T();
        gTrinity.esloco.free(obj);
    }

    static void* tmMalloc(size_t size) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        void* obj = gTrinity.esloco.malloc(size);
        return obj;
    }

    static void tmFree(void* obj) {
        if (obj == nullptr) return;
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not de-allocate outside a transaction\n");
            return;
        }
        gTrinity.esloco.free(obj);
    }

    static void* pmalloc(size_t size) {
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not allocate outside a transaction\n");
            return nullptr;
        }
        return gTrinity.esloco.malloc(size);
    }

    static void pfree(void* obj) {
        if (obj == nullptr) return;
        if (tl_opdata == nullptr) {
            printf("ERROR: Can not de-allocate outside a transaction\n");
            return;
        }
        gTrinity.esloco.free(obj);
    }

    // Get a root pointer
    static inline void* get_object(int idx) {
        return ((persist<void*>*)pmd->root)[idx].pload();
    }

    // Set a root pointer
    static inline void put_object(int idx, void* obj) {
        ((persist<void*>*)pmd->root)[idx].pstore(obj);
    }
};


//
// Wrapper methods to the global TM instance. The user should use these:
//
template<typename R, typename F> static R updateTx(F&& func) { return gTrinity.transaction<R>(func, TX_IS_UPDATE); }
template<typename R, typename F> static R readTx(F&& func) { return gTrinity.transaction<R>(func, TX_IS_READ); }
template<typename F> static void updateTx(F&& func) { gTrinity.transaction(func, TX_IS_UPDATE); }
template<typename F> static void readTx(F&& func) { gTrinity.transaction(func, TX_IS_READ); }
template<typename T, typename... Args> T* tmNew(Args&&... args) { return Trinity::tmNew<T>(args...); }
template<typename T> void tmDelete(T* obj) { Trinity::tmDelete<T>(obj); }
static void* tmMalloc(size_t size) { return Trinity::tmMalloc(size); }
static void tmFree(void* obj) { Trinity::tmFree(obj); }


#ifndef INCLUDED_FROM_MULTIPLE_CPP
//
// Place these in a .cpp if you include this header from multiple files (compilation units)
//
// Global/singleton to hold all the thread registry functionality
ThreadRegistry gThreadRegistry {};
// Array of locks
std::atomic<uint64_t> *gHashLock {nullptr};
// Global clock for TL2
alignas(128) std::atomic<uint64_t> gClockPaddingA {0};
alignas(128) std::atomic<uint64_t> gClock {1};
alignas(128) std::atomic<uint64_t> gClockPaddingB {0};
// PTM singleton
Trinity gTrinity {};
// Thread-local data of the current ongoing transaction
thread_local OpData* tl_opdata {nullptr};
// This is where every thread stores the tid it has been assigned when it calls getTID() for the first time.
// When the thread dies, the destructor of ThreadCheckInCheckOut will be called and de-register the thread.
thread_local ThreadCheckInCheckOut tl_tcico {};
// Helper function for thread de-registration
void thread_registry_deregister_thread(const int tid) {
    gThreadRegistry.deregister_thread(tid);
}
// This is called from persist::load()/store() and endTx() if the read-set validation fails.
// [[noreturn]] void abortTx(OpData* myd) {
void abortTx(OpData* myd) {
    myd->writeSet.rollbackVR(myd->tid);
    uint64_t nextClock = gClock.fetch_add(1)+1;
    // Unlock with the new sequence
    myd->writeSet.unlock(nextClock, myd->tid);
    myd->numAborts++;
    std::longjmp(myd->env, 1);
}
#endif // INCLUDED_FROM_MULTIPLE_CPP

}

