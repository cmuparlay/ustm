#ifndef ESLOCO_LIB_H
#define ESLOCO_LIB_H
/*
 *   This is ported from RedoDB original authors: 
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 * 
 *   The intention is to test if this simple allocator with the persistent numa aware black box UC
 *      
 *   Originally EsLoco was used with the authors persist<> type however, the persist<> type did not
 *      interpose stores or loads (which we don't want for UCs anyways) so I 
 * 
 * 
 * 
 * 
 * This is currently a single threaded allocator - it is lacking safeguards
 *      or other mechanisms to prevent problems if concurrent calls to malloc/free
 *      are performed
 * 
 *      I could change this to allow for multiple threads by sharding the
 *      memory pool such that each threads starting address is the TID * shard size
 *      which would prevent any conflicts
 * 
 *      This is not currently needed by the persistnet black box UC which is the only
 *      user of this allocator 
 * 
 * 
 */

#pragma once

/*
 * This is originally from https://github.com/pramalhe/RedoDB/tree/master/ptms
 * 		original authors: 
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 * 
 * Most of this is just adapated from the original with the CXPUC specific stuff removed
 * This was added to setbench when we wanted to test some stuff related to the CXPUC
*/

#include <cstring>
#include <cstdint>
#include <iostream>
#include <cassert>

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cassert>
#include <fcntl.h>
#include <cstdint>
#include <sys/stat.h>
#include <errno.h>

#ifndef tl2
#define tl2
#endif

#include "../ptm.h"

#ifndef  MMAP_FLAGS
    #if defined(MAP_SHARED_VALIDATE) && defined(MAP_SYNC) && !defined(DISABLE_PERSISTENT_MEMORY_USE)
        #define MMAP_FLAGS MAP_SHARED_VALIDATE | MAP_SYNC
    #else 
        #define MMAP_FLAGS MAP_SHARED
    #endif
#endif

void* initializePersistentMemoryFileMapping(int& fd, const char* filePath, size_t maxSize, bool& doRecover, bool checkIfExists = false, void* startAddr = nullptr, uint64_t recoverCode = 0);
void createFile(int& fd, const char* filePath, size_t maxSize, void* startAddr = nullptr);
void recover(int& fd, const char* filePath, size_t maxSize, void* startAddr);	
inline void* mmapFile(int& fd, size_t maxSize, int flags = MMAP_FLAGS, void* startAddr = nullptr);
inline void* annonMmap(size_t maxSize, int flags, void* startAddr = nullptr);


//Nullptr return means you need to recover
//non null means we created new file
//if the mmap results in nullptr then there was an error which will be thrown appropriately
void* initializePersistentMemoryFileMapping(int& fd, const char* filePath, size_t maxSize, bool& doRecover, bool checkIfExists, void* startAddr, uint64_t recoverCode){
#ifndef DISABLE_PERSISTENT_MEMORY_USE
	printf("PMEM file full path: %s\n", filePath);
	printf("PMEM pool size = %ld\n", maxSize);
	
	// Check if the file already exists or not
	struct stat buf;
    if (checkIfExists) {
        if (stat(filePath, &buf) == 0) {      
            //check if hex code is correct          			     
            fd = open(filePath, O_RDWR|O_CREAT, 0755);
            void* gotAddr = mmapFile(fd, maxSize, MMAP_FLAGS, startAddr);
            if (*((uint64_t*)gotAddr) == recoverCode) {
                doRecover = true;
            }
            else {
                doRecover = false;
            }
            return gotAddr;   			
        } else {
            createFile(fd, filePath, maxSize, startAddr);
        }
    } else {
        if (stat(filePath, &buf) == 0) {        			     
            printf("The PMEM file already exits. Removing old file and recreating.\n");
            remove(filePath);  			
        }
        createFile(fd, filePath, maxSize, startAddr);
    }
	void* gotAddr = mmapFile(fd, maxSize, MMAP_FLAGS, startAddr);
#else
    void* gotAddr = annonMmap(maxSize, MAP_SHARED, startAddr);
#endif
	return gotAddr;
}

void createFile(int& fd, const char* filePath, size_t maxSize, void* startAddr){
	fd = open(filePath, O_RDWR|O_CREAT, 0755);
	assert(fd >= 0);
	if (lseek(fd, maxSize-1, SEEK_SET) == -1) {
		perror("lseek() error");
	}
	if (write(fd, "", 1) == -1) {
		perror("write() error");
	}
}

void* mmapFile(int& fd, size_t maxSize, int flags, void* startAddr){
	void* gotAddr = mmap(startAddr, maxSize, (PROT_READ | PROT_WRITE), flags, fd, 0);
	if (gotAddr == MAP_FAILED) {
		printf("ERROR: mmap() is not working ...\n");		
		assert(false);
		exit(-1);
	}
	if (startAddr && gotAddr != startAddr) {
		printf("NOTE: mmap() got different address than expected...\n");
		printf("gotAddr = %p instead of %p\n", gotAddr, startAddr);
	}

	return gotAddr;
}

void* annonMmap(size_t maxSize, int flags, void* startAddr){
    void* gotAddr = mmap(startAddr, maxSize, (PROT_READ | PROT_WRITE), flags | MAP_ANONYMOUS, -1, 0);
	if (gotAddr == MAP_FAILED) {
        printf("ERRNO: %s\n", strerror(errno));
		printf("ERROR: mmap() returned MAP_FAILED\n Exiting\n\n");		
		assert(false);
		exit(-1);
	}
	if (startAddr && gotAddr != startAddr) {
		printf("NOTE: mmap() got different address than expected...\n");
		printf("gotAddr = %p instead of %p\n", gotAddr, startAddr);
        //TODO:: would need to save the base address in PMEM so we can mmap to this address again
        //  next time. Ideally we just always mmap to the same address but thats not possible
        //  to guarantee
	}

	return gotAddr;
}







// #define ESLOCO_PMEM_FILE_PATH "/mnt/pmem1_mount/esloco_guy"
// #define ESLOCO_PMEM_POOL_SIZE (64 * 1024 * 1024 * 1024ULL) //64GB
// #define ESLOCO_PMEM_POOL_SIZE (8 * 1024 * 1024 * 1024ULL) //8GB
// #define ESLOCO_PMEM_POOL_SIZE (1024 * 1024 * 1024ULL) //1GB
// #define ESLOCO_POOL_VMEM_BASE_ADDR 0x7fddc0000000
// #define ESLOCO_POOL_PMEM_BASE_ADDR 0x7fdfc0000000 //8GB past vmem pool
// #define ESLOCO_CLEAR_POOL true

#ifndef likely
    #define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
    #define unlikely(x)     __builtin_expect((x),0)
#endif


// #define ESLOCO_MAX_THREADS 124
// extern __thread int tid;


struct esloco_block {
    esloco_block*   next;   // Pointer to next block in free-list (when block is in free-list)
    uint64_t size;   // Exponent of power of two of the size of this block in bytes.
};

typedef struct _eslocoAlloc {
    bool isInitialized = false;    
    uint8_t* poolAddr {nullptr};
    uint64_t poolSize {0};
    // Volatile pointer to array of persistent heads of free-list (blocks)
    esloco_block* freelists {nullptr};
    // Volatile pointer to persistent pointer to last unused address (the top of the pool)
    uint8_t* poolTop {nullptr};
} EslocoAlloc;

thread_local EslocoAlloc __eslocoAlloc;

#define ESLOCO_MAX_BLOCK_SIZE 40


//--------------------------------------------------------------------------------
//                              OLD FORMAT FROM C++
//--------------------------------------------------------------------------------
// bool isInitialized = false;

// Volatile data
// uint8_t* poolAddr {nullptr};
// uint64_t poolSize {0};

// // Volatile pointer to array of persistent heads of free-list (blocks)
// esloco_block* freelists {nullptr};
// // Volatile pointer to persistent pointer to last unused address (the top of the pool)
// uint8_t* poolTop {nullptr};

// Number of blocks in the freelists array.
// Each entry corresponds to an exponent of the block size: 2^4, 2^5, 2^6... 2^40
// static const int kMaxBlockSize = 40; // 1 TB of memory should be enough
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------
//--------------------------------------------------------------------------------


// For powers of 2, returns the highest bit, otherwise, returns the next highest bit
uint64_t highestBit(uint64_t val) {
    uint64_t b = 0;
    while ((val >> (b+1)) != 0) b++;
    if (val > (1 << b)) return b+1;
    return b;
}

uint8_t* aligned(uint8_t* addr) {
    return (uint8_t*)((size_t)addr & (~0x3FULL)) + 128;
}

void initEslocoAlloc (int tid, void* mapStartAddr, uint64_t poolSize, bool clearPool = true) {    
    if (!mapStartAddr) {
        printf("Error: Esloco requires that you provide a base address when initializing but no base address was provided.\n");
        exit(-1);
    }
        
    __eslocoAlloc.poolSize = poolSize;
    __eslocoAlloc.poolAddr = (uint8_t*)((uint64_t)mapStartAddr + (poolSize * tid)); 
    __eslocoAlloc.poolTop = (uint8_t*)__eslocoAlloc.poolAddr;
    __eslocoAlloc.freelists = (esloco_block*)(__eslocoAlloc.poolAddr + sizeof(*__eslocoAlloc.poolTop));
    
    if (clearPool) {            
        for (int i = 0; i < ESLOCO_MAX_BLOCK_SIZE; i++) __eslocoAlloc.freelists[i].next = (nullptr);
        
        __eslocoAlloc.poolTop = (aligned(__eslocoAlloc.poolAddr + sizeof(*__eslocoAlloc.poolTop) + sizeof(esloco_block) * ESLOCO_MAX_BLOCK_SIZE));
    }      

    __eslocoAlloc.isInitialized = true;
}




   
void* esloco_malloc(size_t size) {
    if (unlikely(!__eslocoAlloc.isInitialized)) {
        printf("Error: must initialize Esloco before first call to malloc.\n Exiting.\n");
        exit(-1);
    }

    //uint8_t* top = poolTop;
    // Adjust size to nearest (highest) power of 2
    uint64_t bsize = highestBit(size + sizeof(esloco_block));

    esloco_block* myblock = nullptr;
    // Check if there is a block of that size in the corresponding freelist
    if (__eslocoAlloc.freelists[bsize].next != nullptr) {
        // Unlink block
        myblock = __eslocoAlloc.freelists[bsize].next;
        __eslocoAlloc.freelists[bsize].next = myblock->next;
    } else {
        // Couldn't find a suitable block, get one from the top of the pool if there is one available
        // if (top + (1<<bsize) > poolSize + poolAddr) return nullptr;
        // myblock = (block*)top;
        //top = (top + (1<<bsize));      
        if (__eslocoAlloc.poolTop + (1<<bsize) > __eslocoAlloc.poolSize + __eslocoAlloc.poolAddr) return nullptr;
        myblock = (esloco_block*)__eslocoAlloc.poolTop;
        __eslocoAlloc.poolTop = (__eslocoAlloc.poolTop + (1<<bsize));      
        myblock->size = bsize;                               
    }
    // Return the block, minus the header
    return (void*)((uint8_t*)myblock + sizeof(esloco_block));
}


// Takes a pointer to an object and puts the block on the free-list.
void esloco_free(void* ptr) {
    if (ptr == nullptr) return;
    esloco_block* myblock = (esloco_block*)((uint8_t*)ptr - sizeof(esloco_block));
    // Insert the block in the corresponding freelist
    uint64_t size = myblock->size;
    myblock->next = __eslocoAlloc.freelists[size].next;    
    __eslocoAlloc.freelists[size].next = myblock;                  
}




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
 * | poolTop | gfreelists[0..50] | threadflists[0..MAX_THREADS_POW2] | ... allocated objects ... |
 * ---------------------------------------------------------------------------------------------------
 */


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
        block*   next;    // Pointer to next block in free-list (when block is in free-list)
        uint64_t size2;   // Exponent of power of two of the size of this block in bytes.
    };

    // A persistent free-list of blocks
    struct FList {
        uint64_t count;   // Number of blocks in the stack
        block*   head;    // Head of the stack (where we push and pop)
        block*   tail;    // Tail of the stack (used for faster transfers to/from gflist)

        // Push a block into this free list, incrementing the volatile
        // counter and adjusting the tail if needed. Does two pstores min.
        void pushBlock(block* myblock) {
            block* lhead = head;
            if (lhead == nullptr) {
                assert(count == 0);
                tail = myblock;                    
            }
            myblock->next = lhead;                 
            head = myblock;                        
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
            head = myblock->next;                
            if (head == nullptr) {
                tail = nullptr; 
            }
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
                oflist->tail = tail;                           
            }
            tail->next = oflist->head;                         
            oflist->head = head;                               
            oflist->count += count;                            
            count = 0;                                         
            tail = nullptr;                                    
            head = nullptr;                                    
        }
    };

    // To avoid false sharing and lock conflicts, we put each thread-local data in one of these.
    // This is persistent (in PM).
    struct TLData {
        uint64_t     padding[8];
        // Thread-local array of persistent heads of free-list
        FList        tflists[kMaxBlockSize];
        // Thread-local slab pointer
        uint8_t*  slabtop;
        // Thread-loal slab size
        uint64_t  slabsize;
        TLData() {
            for (int i = 0; i < kMaxBlockSize; i++) {
                tflists[i].count = 0;             
                tflists[i].head = nullptr;        
                tflists[i].tail = nullptr;        
            }
            slabtop = nullptr;                    
            slabsize = 0;                         
        }
    };

    // Complete metadata. This is PM
    struct ELMetadata {
        uint8_t*    top;
        FList       gflists[kMaxBlockSize];
        TLData      tldata[MAX_THREADS_POW2];
        // This constructor should be called from within a transaction
        ELMetadata() {
            // The 'top' starts after the metadata header
            top = ((uint8_t*)this) + sizeof(ELMetadata);         
            for (int i = 0; i < kMaxBlockSize; i++) {
                gflists[i].count = 0;             
                gflists[i].head = nullptr;        
                gflists[i].tail = nullptr;        
            }
            for (int it = 0; it < MAX_THREADS_POW2; it++) {
                new (&tldata[it]) TLData();                      
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
        std::memset(poolAddr, 0, sizeof(block)*kMaxBlockSize*(MAX_THREADS_POW2+1));
        meta->top = nullptr;
    }

    // Returns the number of bytes that may (or may not) have allocated objects, from the base address to the top address
    uint64_t getUsedSize() {
        return meta->top - poolAddr;
    }

    // Takes the desired size of the object in bytes.
    // Returns pointer to memory in pool, or nullptr.
    // Does on average 1 store to persistent memory when re-utilizing blocks.
    void* malloc(size_t size) {        
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
            if (asize >= meta->tldata[tid].slabsize) {
                if (debugOn) printf("Not enough space on current slab of thread %d for %ld bytes. Taking new slab from top\n",tid, asize);
                // Not enough space for allocation. Take a new slab from the global top and
                // discard the old one. This means fragmentation/leak is at most kSlabSize/2.
                if (meta->top + kSlabSize > poolSize + poolAddr) {
                    printf("EsLoco2: Out of memory for slab %ld for %ld bytes allocation\n", kSlabSize, size);
                    return nullptr;
                }
                meta->tldata[tid].slabtop = meta->top;   
                meta->tldata[tid].slabsize = kSlabSize;          
                meta->top = (meta->top + kSlabSize); 
                // TODO: add the remains of the slab to thread-local freelist
            }
            // Take space from the slab
            myblock = (block*)meta->tldata[tid].slabtop;
            myblock->size2 = bsize;                                      
            meta->tldata[tid].slabtop = ((uint8_t*)myblock + asize); 
            meta->tldata[tid].slabsize -= asize;                         
        } else if (!meta->gflists[bsize].isEmpty()) {
            if (debugOn) printf("Found available block in thread-global freelist\n");
            myblock = meta->gflists[bsize].popBlock();
        } else {
            if (debugOn) printf("Creating new block from top, currently at %p\n", meta->top);
            // Couldn't find a suitable block, get one from the top of the pool if there is one available
            if (meta->top + asize > poolSize + poolAddr) {
                printf("EsLoco2: Out of memory for %ld bytes allocation\n", size);
                return nullptr;
            }
            myblock = (block*)meta->top;
            meta->top = (meta->top + asize);                 
            myblock->size2 = bsize;                                       
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
        block* myblock = (block*)((uint8_t*)ptr - sizeof(block));
        if (debugOn) printf("free:%ld %p\n", 1UL << myblock->size2, ptr);
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

extern thread_local int tid;
EsLoco2 esloco2;

#endif