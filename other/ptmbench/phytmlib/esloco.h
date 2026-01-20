#pragma once

template <template <typename> class P>
class EsLoco2 {
private:
    const bool debugOn = false;
    // Number of blocks in the freelists array.
    // Each entry corresponds to an exponent of the block size: 2^4, 2^5, 2^6... 2^40
    static const int kMaxBlockSize = 50; // 1024 TB of memory should be enough
    // Size after which we trigger a cleanup of the thread-local list/pool
    static const uint64_t kMaxListSize = 128;
    // Size of a thread-local slab (in bytes). When it runs out, we take another slab from poolTop.
    // Larger allocations go directly on poolTop which means doing a lot of large allocations from
    // multiple threads will result in contention.
    static const uint64_t kSlabSize = 8*1024*1024;


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
            count.store(count.load() + 1);
        }

        // Pops a block from a given (persistent) free list, decrementing the
        // volatile counter and setting the tail to nullptr if list becomes empty.
        // Does 2 pstores.
        block* popBlock(void) {
            assert(count != 0);
            assert(tail != nullptr);
            count.store(count.load() - 1);
            block* myblock = head.load();
            head = myblock->next.load();                // pstore()
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
                oflist->tail = tail.load();                    // pstore()
            }
            tail->next = oflist->head.load();                         // pstore()
            oflist->head = head.load();                               // pstore()
            oflist->count = oflist->count.load() + count.load();                            // pstore()
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

#ifdef ESLOCO_ENABLE_DEFERRED_FREE
        P<void*> deferredFreeObjArray[MAX_DEFERRED_FREES];
        P<uint64_t> deferedFreeCount;
#endif

        TLData() {
            for (int i = 0; i < kMaxBlockSize; i++) {
                tflists[i].count = 0;             // pstore()
                tflists[i].head = nullptr;        // pstore()
                tflists[i].tail = nullptr;        // pstore()
            }
            slabtop = nullptr;                    // pstore()
            slabsize = 0;                         // pstore()
#ifdef ESLOCO_ENABLE_DEFERRED_FREE            
            deferedFreeCount = 0;
#endif
        }
    };

    // Complete metadata. This is PM
    struct ELMetadata {
        P<uint8_t*> top;
        FList       gflists[kMaxBlockSize];
        TLData      tldata[MAX_THREADS_POW2];
        // This constructor should be called from within a transaction
        ELMetadata() {
            // The 'top' starts after the metadata header
            // top = ((uint8_t*)this) + sizeof(ELMetadata);         // pstore()
            top = ((uint8_t*)this) + sizeof(ELMetadata) + (MAX_THREADS_POW2 * kSlabSize);
            for (int i = 0; i < kMaxBlockSize; i++) {
                gflists[i].count = 0;             // pstore()
                gflists[i].head = nullptr;        // pstore()
                gflists[i].tail = nullptr;        // pstore()
            }
            for (int it = 0; it < MAX_THREADS_POW2; it++) {
                new (&tldata[it]) TLData();                      // pstores()
                tldata[it].slabtop = ((uint8_t*)this) + sizeof(ELMetadata) + (it * kSlabSize);
                tldata[it].slabsize = kSlabSize;
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
    void init(void* addressOfMemoryPool, size_t sizeOfMemoryPool, int numThreads = MAX_THREADS_POW2, bool clearPool=true) {
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
        std::memset(poolAddr, 0, sizeof(block)*kMaxBlockSize*(MAX_THREADS_POW2+1));
        meta->top.store(nullptr);
    }

    // Returns the number of bytes that may (or may not) have allocated objects, from the base address to the top address
    uint64_t getUsedSize() {
        return meta->top.load() - poolAddr;
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
            if (asize >= meta->tldata[tid].slabsize.load()) {
                if (debugOn) printf("Not enough space on current slab of thread %d for %ld bytes. Taking new slab from top\n",tid, asize);
                // Not enough space for allocation. Take a new slab from the global top and
                // discard the old one. This means fragmentation/leak is at most kSlabSize/2.
                if (meta->top.load() + kSlabSize > poolSize + poolAddr) {
                    printf("EsLoco2: Out of memory for slab %ld for %ld bytes allocation\n", kSlabSize, size);
                    return nullptr;
                }
                meta->tldata[tid].slabtop = meta->top.load();   // pstore()
                meta->tldata[tid].slabsize = kSlabSize;          // pstore()
                meta->top.store(meta->top.load() + kSlabSize); // pstore()
                // TODO: add the remains of the slab to thread-local freelist
            }
            // Take space from the slab
            myblock = (block*)meta->tldata[tid].slabtop.load();
            myblock->size2 = bsize;                                      // pstore()
            meta->tldata[tid].slabtop.store((uint8_t*)myblock + asize); // pstore()
            meta->tldata[tid].slabsize = meta->tldata[tid].slabsize - asize;                         // pstore()
        } else if (!meta->gflists[bsize].isEmpty()) {
            if (debugOn) printf("Found available block in thread-global freelist\n");
            myblock = meta->gflists[bsize].popBlock();
        } else {
            if (debugOn) printf("Creating new block from top, currently at %p\n", meta->top.load());
            // Couldn't find a suitable block, get one from the top of the pool if there is one available
            if (meta->top.load() + asize > poolSize + poolAddr) {
                printf("EsLoco2: Out of memory for %ld bytes allocation\n", size);
                return nullptr;
            }
            myblock = (block*)meta->top.load();
            meta->top.pstore(meta->top.load() + asize);                 // pstore()
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
        if (debugOn) printf("free:%ld %p\n", 1UL << myblock->size2.load(), ptr);
        // Insert the block in the corresponding freelist
        uint64_t bsize = myblock->size2;
        meta->tldata[tid].tflists[bsize].pushBlock(myblock);
        if (meta->tldata[tid].tflists[bsize].isFull()) {
            if (debugOn) printf("Moving thread-local flist for bsize=%ld to thread-global flist\n", bsize);
            // Move all objects in the thread-local list to the corresponding thread-global list
            meta->tldata[tid].tflists[bsize].transfer(&meta->gflists[bsize]);
        }
    }

#ifdef ESLOCO_ENABLE_DEFERRED_FREE
    void deferFree(void* ptr) {
        if (ptr == nullptr) return;
        const int tid = ((Thread_void*)(__tm_self))->UniqID;
        uint64_t dfCount = meta->tldata[tid].deferedFreeCount.load();
        meta->tldata[tid].deferredFreeObjArray[dfCount] = ptr;
        meta->tldata[tid].deferedFreeCount = dfCount + 1;
        assert(meta->tldata[tid].deferedFreeCount < MAX_DEFERRED_FREES);
    }

    void doDeferedFrees() {
        const int tid = ((Thread_void*)(__tm_self))->UniqID;
        uint64_t dfCount = meta->tldata[tid].deferedFreeCount.load();
        for (int i = 0; i < dfCount; i++) {
            this->free(meta->tldata[tid].deferredFreeObjArray[i].load());
        }
        meta->tldata[tid].deferedFreeCount = 0;
    }
#endif
};