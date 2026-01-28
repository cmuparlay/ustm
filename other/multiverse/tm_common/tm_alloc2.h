// #pragma once
#ifndef SIMPLE_TM_ALLOC_H
#define SIMPLE_TM_ALLOC_H

#include <stdlib.h>
#include <cstdint>
#include <stdio.h>
#include <unistd.h>
#include <sys/mman.h>
#include <cassert>
#include <fcntl.h>

// #define LOG_PAGE_INIT
#ifdef LOG_PAGE_INIT
	#include <iostream>
	#include <fstream>
#endif

// #define ALLOC_DEBUG
// #define ALLOC_DEBUG2

#ifndef PAD
#define CAT2(x, y) x##y
#define CAT(x, y) CAT2(x, y)
#define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#define TM_ALLOC_FAA __sync_fetch_and_add
#define TM_ALLOC_BCAS __sync_bool_compare_and_swap
#define TM_ALLOC_PAUSE __asm__ __volatile__("pause;")

#ifndef SOFTWARE_BARRIER
    #define SOFTWARE_BARRIER asm volatile("": : :"memory")
#endif

#ifndef TM_ALLOC_PAGE_SIZE_BYTES
	#define TM_ALLOC_PAGE_SIZE_BYTES 4*1024*1024ULL
#endif

#ifndef MAX_THREADS_POW2
	#define MAX_THREADS_POW2 256
#endif

// static const int TM_ALLOC_BUFFER_SIZE = 64
static const int TM_ALLOC_BUFFER_SIZE = 256;

// this needs renaming - using -1 to indicate allocations that spanned more than 1 page
// these are special 'overflow' allocations where we allocate / free by multiple pages
// and we dont reset the pages meaning their free list is always empty
static const int OVERFLOW_PAGE_SIZE_CLASS_POW2 = -1;

static const uint64_t PAGE_MASK = ~(TM_ALLOC_PAGE_SIZE_BYTES - 1);

static const int PAGE_IS_FULL = 1;

using std::uint64_t;

// class TMAlloc;
// extern TMAlloc tmAlloc;

class TMAlloc {
private:
	typedef uintptr_t Block;	

	PAD;

	struct MemoryPage{		
		PAD;
		uint64_t ownerTid;
		void* start;	
		uint64_t sizeClassPow2;
		uint64_t sizeClassSize;				
		PAD;
		Block* freeList_head;		
		PAD;
		volatile int sharedPageData_lock;
		volatile uint64_t isFull;
		MemoryPage* volatile next;
		MemoryPage* volatile prev;
		PAD;		
		volatile int crossThreadFreeList_lock;
		Block* crossThreadFreeList_head;
		#ifdef ALLOC_DEBUG
		PAD;
		uint64_t numFreeObj;		
		#endif
		PAD;
		uint8_t pad[296];
	};
	PAD;

	struct BufferNode {
		BufferNode* next;
		Block* obj;
	};
	PAD;

	static constexpr uint64_t TM_ALLOC_USABLE_PAGE_SIZE_BYTES = (TM_ALLOC_PAGE_SIZE_BYTES - sizeof(MemoryPage));		
	static constexpr uint64_t MAX_ALLOC_POW2_SIZE_CLASS = 16;

// #ifndef MAX_ALLOC_POW2_SIZE_CLASS
// 	#define MAX_ALLOC_POW2_SIZE_CLASS 16 //2^16 = 65536
// #endif

	struct ThreadData {
		PAD;
		MemoryPage* nonFullPageListsPerSizeClass[MAX_ALLOC_POW2_SIZE_CLASS];
		// PAD;

		MemoryPage* volatile fullPageList_head;
		volatile int fullPageList_lock;
		// PAD;
		
		MemoryPage* volatile recheckPageListPerSizeClass[MAX_ALLOC_POW2_SIZE_CLASS];
		volatile int recheckPageList_lock;
		// PAD;

		MemoryPage* volatile overflowPageList_head;
		volatile int overflowPageList_lock;
		PAD;

		BufferNode allocBuffer[TM_ALLOC_BUFFER_SIZE];
		BufferNode* allocBufferTail;
		uint64_t allocCount;
		// PAD;

		BufferNode freeBuffer[TM_ALLOC_BUFFER_SIZE];
		BufferNode* freeBufferTail;
		uint64_t freeCount;
		PAD;
		// long garbage = 0;		
		#ifdef ALLOC_DEBUG
		PAD;
		uint32_t debugAllocCount = 0;
		uint32_t debugFreeCount = 0;
		uint32_t debugNumCommit = 0;
		uint32_t debugNumAbort = 0;
		PAD;
		#endif
	};

	volatile uint64_t numAllocatedPages = 0;
	PAD;

	MemoryPage* volatile emptyOverFlowPageList_head;
	volatile uint64_t emptyOverFlowPageList_size = 0;
	volatile int emptyOverFlowPageList_lock;	
	PAD;

	uint8_t* baseAddr;
	uint64_t mmapSize;
	uint64_t totalPages;
	PAD;

	ThreadData tData[MAX_THREADS_POW2];
	PAD;


	inline uint64_t getSizeClass(uint64_t size) {
		uint64_t bitIndex = 0;
		while (size >> (bitIndex+1)) {
			bitIndex++;
		}
		if (size > (1ULL << bitIndex)) {
			bitIndex++;
		}
		return bitIndex;
	}

	inline void acquireLock(volatile int* lock) {
		while (1) {
			if (*lock) {
				TM_ALLOC_PAUSE;
				continue;
			}
			if (TM_ALLOC_BCAS(lock, 0, 1)) {
				return;
			}
		}
	}

	inline void releaseLock(volatile int* lock) {
		SOFTWARE_BARRIER;
		*lock = 0;
	}

	inline void* tryAlloc(int tid, uint64_t sizeClass) {
		ThreadData* td = &tData[tid];				
		MemoryPage* page = td->nonFullPageListsPerSizeClass[sizeClass];

		if (!page) {
			//we dont have any non full pages
			return nullptr;
		}

		//do we have an object in our freelist?
		if (page->freeList_head) {			
			Block* ptr = page->freeList_head;
			// SOFTWARE_BARRIER;
			page->freeList_head = (Block*)(*((uintptr_t*)(page->freeList_head)));
			assert(((uint64_t)page->freeList_head) < ((uint64_t)page + (TM_ALLOC_PAGE_SIZE_BYTES)));				
			assert(!page->freeList_head || (page->freeList_head && (*page->freeList_head || *page->freeList_head == 0)));
			#ifdef ALLOC_DEBUG
			assert(page->numFreeObj > 0);
			page->numFreeObj--;
			#endif
			return (void*)ptr;
		}

		if (page->crossThreadFreeList_head) {			
			acquireLock(&(page->crossThreadFreeList_lock));			
			//we only move the cross thread free list if our free list is empty
			// so we dont need to do any appends and we dont need a tail ptr			
			page->freeList_head = page->crossThreadFreeList_head;
			assert(!page->freeList_head || (page->freeList_head && (*page->freeList_head || *page->freeList_head == 0)));
			// SOFTWARE_BARRIER;
			page->crossThreadFreeList_head = nullptr;
			releaseLock(&(page->crossThreadFreeList_lock));

			//we could have done this before moving the cross thread free list
			// but its the same number of writes and we get to release the lock sooner this way
			Block* ptr = page->freeList_head;
			//advance free list head to next
			page->freeList_head = (Block*)(*((uintptr_t*)(page->freeList_head)));
			assert(!page->freeList_head || (page->freeList_head && (*page->freeList_head || *page->freeList_head == 0)));
			#ifdef ALLOC_DEBUG
			assert(page->numFreeObj > 0);
			page->numFreeObj--;
			#endif
			return (void*)ptr;
		}

		//this page is actually full it needs to be moved from the non-full to full lists
		// returning nullptr in this case
		return nullptr;
	}

	inline void undoMallocs(int tid) {
		ThreadData* td = &tData[tid];		
		BufferNode* mallocBuff = td->allocBuffer;
		td->freeCount = 0;
		td->freeBufferTail = td->freeBuffer;
		for (uint64_t i = 0; i < td->allocCount; i++) {
			this->tmFree(tid, mallocBuff->obj);
			mallocBuff = mallocBuff->next;
		}
		doDeferredFrees(tid);
	}
	
	inline void removeFullPageFromNonFullList(ThreadData* td, uint64_t sizeClass) {
		// SOFTWARE_BARRIER;		
		//move the page to full page list and need to get a new one						
		MemoryPage* newFullPage = td->nonFullPageListsPerSizeClass[sizeClass];		
		uint64_t isFull = newFullPage->isFull;
		assert(!(isFull & PAGE_IS_FULL));
		assert(newFullPage == td->nonFullPageListsPerSizeClass[sizeClass]);

		//advance to next non-full page
		td->nonFullPageListsPerSizeClass[sizeClass] = td->nonFullPageListsPerSizeClass[sizeClass]->next;						
																	
		// newFullPage->isFull = true;										

		assert(newFullPage->prev == nullptr);

		acquireLock(&(td->fullPageList_lock));
		isFull = newFullPage->isFull;
		
		#ifdef ALLOC_DEBUG
		assert(newFullPage->numFreeObj == 0);
		#else
		assert(!(isFull & PAGE_IS_FULL));
		#endif
		
		newFullPage->isFull++;
		#ifdef ALLOC_DEBUG
		printf("made page %p full\n", newFullPage);
		#endif	

		MemoryPage* oldFullPageListHead = td->fullPageList_head;					
		newFullPage->next = oldFullPageListHead;
		if (oldFullPageListHead) {
			oldFullPageListHead->prev = newFullPage;
		}				
		td->fullPageList_head = newFullPage;				
		releaseLock(&(td->fullPageList_lock));
	}

	inline void localFree(ThreadData* td, Block* obj, MemoryPage* page) {
		//regular free					
		*((uintptr_t*)obj) = (uintptr_t)(page->freeList_head);	
		assert(*obj || *obj == 0);
		page->freeList_head = obj;
#ifdef ALLOC_DEBUG
		page->numFreeObj++;
#endif
		uint64_t isFull = page->isFull;
		//check if page was full and move it if it was
		if (isFull & PAGE_IS_FULL) {
			if (!TM_ALLOC_BCAS(&page->isFull, isFull, isFull+1)) {
				return;
			}
			else {
#ifdef ALLOC_DEBUG
				printf("made page %p non-full\n", page);
#endif	
			}

			//remove the page from the full page list
			acquireLock(&(td->fullPageList_lock));
			if ((page->isFull & PAGE_IS_FULL)) {
				releaseLock(&(td->fullPageList_lock));	
				exit(-1); //should never get here we either CAS to not full or goto next obj
				// goto NEXT_OBJ;
			}
			
			MemoryPage* pnext = page->next;
			MemoryPage* pprev = page->prev;
			if (pnext) {
				pnext->prev = pprev;
			}
			if (pprev) {
				pprev->next = pnext;
			}
			if (td->fullPageList_head == page) {
				td->fullPageList_head = td->fullPageList_head->next;
			}

			//move the page to the nonfull page list
			MemoryPage* oldHead = td->nonFullPageListsPerSizeClass[page->sizeClassPow2];
			// page->isFull = false;	
			page->prev = nullptr;
			page->next = oldHead;
			if (oldHead) {
				oldHead->prev = page;
			}
			td->nonFullPageListsPerSizeClass[page->sizeClassPow2] = page;
			//we wait to release the lock on the full page list 
			//to guard the scenario where 2 threads are freeing from the same
			//full page
			releaseLock(&(td->fullPageList_lock));		
#ifdef ALLOC_DEBUG3
			printf("Tid: %d, free from full page: %p - moved to non full\n", tid, page);
#endif							
		}
	}

	inline void crossThreadFree(ThreadData* ownerTd, Block* obj, MemoryPage* page) {
		//cross thread free
		acquireLock(&(page->crossThreadFreeList_lock));
		*((uintptr_t*)obj) = (uintptr_t)(page->crossThreadFreeList_head);
		assert(*obj || *obj == 0);
		page->crossThreadFreeList_head = obj;
#ifdef ALLOC_DEBUG
		page->numFreeObj++;
#endif						
		// td->garbage += *((uintptr_t*)(page->crossThreadFreeList_head));
		releaseLock(&(page->crossThreadFreeList_lock));

		uint64_t isFull = page->isFull;
		//check if page was full and move it if it was
		if (isFull & PAGE_IS_FULL) {
			if (!TM_ALLOC_BCAS(&page->isFull, isFull, isFull+1)) {
				return;
			}
			else {
#ifdef ALLOC_DEBUG
				printf("made page %p non-full\n", page);
#endif	
			}

			//remove the page from the full page list
			acquireLock(&(ownerTd->fullPageList_lock));
			//need to recheck if the page is still full since
			//another thread could have concurrently moved it
			if ((page->isFull & PAGE_IS_FULL)) {
				releaseLock(&(ownerTd->fullPageList_lock));
				exit(-1); //should never get here we either CAS to not full or goto next obj
				// goto NEXT_OBJ;
			}

			MemoryPage* next = page->next;
			MemoryPage* prev = page->prev;
			if (next) {
				assert(next);
				page->next->prev = prev;
			}
			if (prev) {
				assert(prev);
				page->prev->next = next;
			}
			if (ownerTd->fullPageList_head == page) {
				ownerTd->fullPageList_head = ownerTd->fullPageList_head->next;
			}

			//move page to the recheck page list
			// need to lock since we dont own the list
			acquireLock(&(ownerTd->recheckPageList_lock));
			MemoryPage* oldHead = ownerTd->recheckPageListPerSizeClass[page->sizeClassPow2];
			// page->isFull = false;	
			page->prev = nullptr;
			page->next = oldHead;
			if (oldHead) {
				oldHead->prev = page;
			}
			ownerTd->recheckPageListPerSizeClass[page->sizeClassPow2] = page;	
			releaseLock(&(ownerTd->recheckPageList_lock));		
			//we wait to release the lock on the full page list 
			//to guard the scenario where 2 threads are freeing from the same
			//full page
			releaseLock(&(ownerTd->fullPageList_lock));
		}
	}

	inline void overflowPageFree(ThreadData* ownerTd, MemoryPage* page) {
		acquireLock(&(ownerTd->overflowPageList_lock));
		acquireLock(&(emptyOverFlowPageList_lock));
		MemoryPage* next = page->next;
		MemoryPage* prev = page->prev;
		if (next) {
			page->next->prev = prev;
		}
		if (prev) {
			page->prev->next = next;
		}
		if (ownerTd->overflowPageList_head == page) {
			ownerTd->overflowPageList_head = ownerTd->overflowPageList_head->next;
		}

		//GUY::NOTES:: removed the below since anyone who ends up
		//	doing another overflow allocation will need the pages to be contiguous
		//	so adding individual pages wont help unless we want to carve them up
		//	again when we overflow allocate which seems bad
		// 	---
		//add the individual pages that make up the overflow allocation
		// since it spans more than 1
		// uint64_t overflowPageCount = (page->sizeClassSize + (TM_ALLOC_USABLE_PAGE_SIZE_BYTES - 1)) / TM_ALLOC_USABLE_PAGE_SIZE_BYTES;
		// for (int ofpcIdx = 0; ofpcIdx < overflowPageCount; ofpcIdx++) {
		// 	MemoryPage* ovPage = (MemoryPage*)(((uintptr_t)page) + (TM_ALLOC_PAGE_SIZE_BYTES * ofpcIdx));
		// 	ovPage->prev = nullptr;
		// 	emptyOverFlowPageList_head->prev = ovPage;
		// 	ovPage->next = emptyOverFlowPageList_head;
		// 	emptyOverFlowPageList_head = ovPage;		
		// }
		// emptyOverFlowPageList_size += overflowPageCount;
		emptyOverFlowPageList_size++;
		MemoryPage* emptyOvfHead = emptyOverFlowPageList_head;				
		page->next = emptyOvfHead;
		emptyOvfHead->prev = page;
		emptyOverFlowPageList_head = page; 
		
		releaseLock(&(emptyOverFlowPageList_lock));
		releaseLock(&(ownerTd->overflowPageList_lock));
	}

	inline void* smallAlloc(int tid, uint64_t sizeClass) {
		ThreadData* td = &tData[tid];
		uint64_t sizeClassSize = 1ULL << sizeClass;
		while (true) {			
			//try allocating in our threads non-full page
			void* ptr = tryAlloc(tid, sizeClass);
			if (ptr) {				
				#ifdef ALLOC_DEBUG2
				printf("Tid: %d requested malloc at %p\n", tid, ptr);
				#endif
				return ptr;
			}

			//if we dont get a ptr from tryAlloc, either the head of our nonFullPageList
			// was full or our non-full page list is empty

			//our non full page was full
			if (td->nonFullPageListsPerSizeClass[sizeClass]) {		
				removeFullPageFromNonFullList(td, sizeClass);
				//check if we have another non-full page in the list							
				if (td->nonFullPageListsPerSizeClass[sizeClass]) {			
					td->nonFullPageListsPerSizeClass[sizeClass]->prev = nullptr;
					#ifdef ALLOC_DEBUG3
					printf("Tid: %d, had another non full page: %p\n", tid, td->nonFullPageListsPerSizeClass[sizeClass]);
					#endif
					continue;
				}
			}			

			//check if we have any pages in the recheck page list and
			// move them to non-full if we do
			// we only get here if our nonFullPageList is empty
			if (td->recheckPageListPerSizeClass[sizeClass]) {				
				acquireLock(&(td->recheckPageList_lock));			
				td->nonFullPageListsPerSizeClass[sizeClass] = td->recheckPageListPerSizeClass[sizeClass];
				td->recheckPageListPerSizeClass[sizeClass] = nullptr;
				releaseLock(&(td->recheckPageList_lock));
				continue;
			}

			//we have no pages or we have filled all of our threads pages we need to get a new one
			uint64_t pageIndex = TM_ALLOC_FAA(&numAllocatedPages, 1);
			if (pageIndex >= totalPages) {
				printf("ERROR WE ARE OUT OF MEMORY IN THIS MAPPING\nEXITING\n");
				exit(-1);
			}
			MemoryPage* page = (MemoryPage*)(((uintptr_t)baseAddr) + (TM_ALLOC_PAGE_SIZE_BYTES * pageIndex));
			page->ownerTid = tid;
			page->sizeClassPow2 = sizeClass;
			page->sizeClassSize = 1ULL << sizeClass;
			page->isFull = 0;
			SOFTWARE_BARRIER;
			resetMemoryPage(page);
			td->nonFullPageListsPerSizeClass[sizeClass] = page;			
			#ifdef ALLOC_DEBUG3
			printf("Tid: %d, assigned page: %p\n", tid, page);
			#endif
		}
	}

	inline void* largeAlloc(int tid, uint64_t size) {
		ThreadData* td = &tData[tid];
		// uint64_t requiredPages = (size + (TM_ALLOC_USABLE_PAGE_SIZE_BYTES - 1)) / TM_ALLOC_USABLE_PAGE_SIZE_BYTES;
		uint64_t requiredPages = (size + (TM_ALLOC_USABLE_PAGE_SIZE_BYTES - 1)) / TM_ALLOC_USABLE_PAGE_SIZE_BYTES;
		requiredPages += 1;
		MemoryPage* page = nullptr;
					
		//TODO:: would be a good idea to clean this up so we can reuse contiguous pages rather than just single pages
		// 		since it is likely that overflow allocs would require 2+ pages anyways
		//		not sure wat the best way to do this is other than just iterating the list 
		// check for pages in global overflow list 
		if (requiredPages == 1 && emptyOverFlowPageList_head) {
			acquireLock(&(emptyOverFlowPageList_lock));
			MemoryPage* currOvfPage = emptyOverFlowPageList_head;
			while (currOvfPage) {
				if (currOvfPage->sizeClassSize <= size) {
					page = currOvfPage;
					break;
				}
				currOvfPage = currOvfPage->next;
			}
			releaseLock(&(emptyOverFlowPageList_lock));

			if (page) {
				return page->start;
			}
		}

		//get fresh pages
		uint64_t pageIndex = TM_ALLOC_FAA(&numAllocatedPages, requiredPages);
		
		if (pageIndex >= totalPages || pageIndex + requiredPages >= totalPages) {
			printf("ERROR WE ARE OUT OF MEMORY IN THIS MAPPING\nEXITING\n");
			exit(-1);
		}			

		page = (MemoryPage*)(((uintptr_t)baseAddr) + (TM_ALLOC_PAGE_SIZE_BYTES * pageIndex));
		memset(page, 0, TM_ALLOC_PAGE_SIZE_BYTES * requiredPages);
		page->start = (void*)(((uintptr_t)page) + sizeof(MemoryPage));	
		page->ownerTid = tid;			
		page->sizeClassPow2 = OVERFLOW_PAGE_SIZE_CLASS_POW2;
		page->sizeClassSize = size;
		page->freeList_head = nullptr;
		page->isFull = 0;
		
		SOFTWARE_BARRIER;		
		acquireLock(&(td->overflowPageList_lock));			
		MemoryPage* oldHead = td->overflowPageList_head;
		page->next = oldHead;
		page->prev = nullptr;
		if (oldHead) {
			oldHead->prev = page;
		}
		td->overflowPageList_head = page;			
		releaseLock(&(td->overflowPageList_lock));
		return page->start;
	}

public:
	inline void doFree(int tid, void* ptr) {
		MemoryPage* page = (MemoryPage*)(uintptr_t(ptr) & PAGE_MASK);								
		uint64_t pageSizeClass = page->sizeClassPow2;
		Block* obj = (Block*)ptr;
		ThreadData* td = &tData[page->ownerTid];
		if (pageSizeClass != OVERFLOW_PAGE_SIZE_CLASS_POW2) {
			if (page->ownerTid == tid) {
				localFree(td, obj, page);
			}
			else {
				crossThreadFree(td, obj, page);
			}
		}
		else {
			overflowPageFree(td, page);
		}
	}
	

	inline void* doMalloc(int tid, size_t size) {
		ThreadData* td = &tData[tid];
		#ifdef ALLOC_DEBUG2
		// printf("Tid: %d requesting malloc of size %ld\n", tid, size);
		td->debugAllocCount++;
		#endif
		size = std::max(size, sizeof(Block*));
		uint64_t sizeClass = getSizeClass(size);  // get the size class
        uint64_t sizeClassSize = 1ULL << sizeClass;
        if (sizeClass < MAX_ALLOC_POW2_SIZE_CLASS && sizeClassSize <= TM_ALLOC_USABLE_PAGE_SIZE_BYTES) {  // if smaller than what fits in a page		
			return smallAlloc(tid, sizeClass);
		}
		else {
			return largeAlloc(tid, size);
		}
	}

private:
	inline void doDeferredFrees(int tid) {		
		//get our own thread's data
		ThreadData* td = &tData[tid];		

		if (td->freeCount == 0) {
			return;
		}
		
		BufferNode* freeBufferNode = td->freeBuffer;
		Block* obj = freeBufferNode->obj;
		#ifdef ALLOC_DEBUG2
		printf("Tid: %d freeing %p\n", tid, obj);
		#endif

		// td->garbage += *((uintptr_t*)obj);

		for (uint64_t i = 0; i < td->freeCount; i++) {
			doFree(tid, obj);
			freeBufferNode = freeBufferNode->next;
			obj = freeBufferNode->obj;			
			#ifdef ALLOC_DEBUG2
			printf("Tid: %d freeing %p\n", tid, obj);
			#endif
		}
	}

	void resetMemoryPage(MemoryPage* page) {
		#ifdef ALLOC_DEBUG
		// printf("resetting page: %p\n", page);
		#endif
		uint64_t numBlocks = TM_ALLOC_USABLE_PAGE_SIZE_BYTES / page->sizeClassSize;
		page->crossThreadFreeList_head = nullptr;
		Block* obj = (Block*)(page->start);
		Block* nextObj = (Block*)(((uintptr_t)page->start) + page->sizeClassSize);
		page->freeList_head = obj;		
		assert(!page->freeList_head || (page->freeList_head && (*page->freeList_head || *page->freeList_head == 0)));
		for (uint64_t i = 0; i < numBlocks-1; i++) {
			*((uintptr_t*)obj) = (uintptr_t)nextObj;			
			// obj++;
			// nextObj++;
			obj = (Block*)(((uintptr_t)obj) + page->sizeClassSize);
			nextObj = (Block*)(((uintptr_t)nextObj) + page->sizeClassSize);
			#ifdef ALLOC_DEBUG
			page->numFreeObj++;
			#endif
		}
		*((uintptr_t*)obj) = (uintptr_t)(nullptr);		
		#ifdef ALLOC_DEBUG
		page->numFreeObj++;
		// printf("%ld free objects\n", page->numFreeObj);
		// printf("page end: %p\n", (void*)((uint64_t)page + (TM_ALLOC_PAGE_SIZE_BYTES)));
		#endif		
	}

	void initThreads() {
		for (int i = 0; i < MAX_THREADS_POW2; i++) {
			for (int szClass = 0; szClass < MAX_ALLOC_POW2_SIZE_CLASS; szClass++) {
				tData[i].nonFullPageListsPerSizeClass[szClass] = nullptr;
				tData[i].recheckPageListPerSizeClass[szClass] = nullptr;
			}
			tData[i].fullPageList_lock = 0;
			tData[i].recheckPageList_lock = 0;
			tData[i].fullPageList_head = nullptr;												
			tData[i].allocCount = 0;	
			tData[i].freeCount = 0;	
			tData[i].allocBufferTail = tData[i].allocBuffer;
			tData[i].freeBufferTail = tData[i].freeBuffer;
			for (int j = 0; j < TM_ALLOC_BUFFER_SIZE - 1; j++) {
				tData[i].allocBuffer[j].next = &tData[i].allocBuffer[j+1];
				tData[i].freeBuffer[j].next = &tData[i].freeBuffer[j+1];
			}					
			tData[i].allocBuffer[TM_ALLOC_BUFFER_SIZE-1].next = nullptr;
			tData[i].freeBuffer[TM_ALLOC_BUFFER_SIZE-1].next = nullptr;
		}
	}

	void initPages() {
		#ifdef LOG_PAGE_INIT
		std::ofstream logFile;
		logFile.open ("allocLog.txt");
		#endif

		// printf("\n");
		for (uint64_t i = 0; i < totalPages; i++) {
			MemoryPage* pageData = (MemoryPage*)(((uintptr_t)baseAddr) + (TM_ALLOC_PAGE_SIZE_BYTES * i));
			pageData->ownerTid = -1;
			pageData->next = nullptr;
			pageData->prev = nullptr;			
			pageData->isFull = false;
			pageData->crossThreadFreeList_lock = 0;
			pageData->crossThreadFreeList_head = nullptr;	
			pageData->start = (void*)(((uintptr_t)pageData) + sizeof(MemoryPage));	
			#ifdef ALLOC_DEBUG
			pageData->numFreeObj = 0;
			#endif			
			
			#ifdef LOG_PAGE_INIT
			logFile << "Page: " << pageData << "\n";
			logFile << "Page start: " << pageData->start << "\n";			
			#endif			
			// printf("Page: %p\n", pageData);
			// printf("Page start: %p\n", pageData->start);			
		}
		// printf("\n");

#ifdef LOG_PAGE_INIT
		logFile.close();
#endif
	}

	void interalInit() {		
		initThreads();		
		initPages();
	}

public:
	TMAlloc() {
		//TODO:: (fix this sometime)
		// stupid sanity check - would have made this a compile time constexpr
		// but compiler not cooperating 
		uint64_t n = 2;
		for (int i = 0; i < MAX_ALLOC_POW2_SIZE_CLASS; i++) {
			n *= 2;
		}
		if (n > TM_ALLOC_USABLE_PAGE_SIZE_BYTES) {
			printf("Config error\n");
			exit(-1);
		}
	}

	~TMAlloc() {		
		// for (int i = 0; i < MAX_THREADS_POW2; i++) {
		// 	printf("garbage %ld\n", tData[i].garbage);
		// }
	}

	inline void reset(int tid) {
		ThreadData* td = &tData[tid];
		td->allocCount = 0;
		td->allocBufferTail = td->allocBuffer;
		td->freeCount = 0;
		td->freeBufferTail = td->freeBuffer;
	}

	inline void commit(int tid) {
		SOFTWARE_BARRIER;		
		#ifdef ALLOC_DEBUG2				
		printf("Tid: %d committing, doing frees\n", tid);
		ThreadData* td = &tData[tid];	
		BufferNode* mallocBuff = td->allocBuffer;		
		for (uint64_t i = 0; i < td->allocCount; i++) {		
			printf("Tid %d allocating %p\n", tid, mallocBuff->obj);
			mallocBuff = mallocBuff->next;
		}		
		#endif

		doDeferredFrees(tid);
		reset(tid);

		#ifdef ALLOC_DEBUG2		
		printf("Tid: %d committed\n", tid);
		tData[tid].debugNumCommit++;
		#endif
	}

	inline void abort(int tid) {
		SOFTWARE_BARRIER;
		#ifdef ALLOC_DEBUG2		
		printf("Tid: %d aborting, undoing mallocs\n", tid);
		tData[tid].debugNumAbort++;
		#endif
		undoMallocs(tid);
		reset(tid);
		#ifdef ALLOC_DEBUG2		
		printf("Tid: %d aborted\n", tid);
		tData[tid].debugNumAbort++;
		#endif
	}

	uint8_t* aligned(uint8_t* addr) {
        // return (uint8_t*)((size_t)addr & (~0x3FULL)) + 128;
        return (uint8_t*)((size_t)addr & (PAGE_MASK)) + TM_ALLOC_PAGE_SIZE_BYTES;
    }

	void init(void* addressOfMemoryPool, size_t sizeOfMemoryPool) {
        baseAddr = aligned((uint8_t*)addressOfMemoryPool);
		mmapSize = sizeOfMemoryPool + (uint8_t*)(addressOfMemoryPool) - baseAddr;
		totalPages = mmapSize / (TM_ALLOC_PAGE_SIZE_BYTES);

		printf("TM Alloc Init:\n");

        printf("\n\nAligned base addr=%p\n\n", baseAddr);

        interalInit();	
			
        printf("\n\nINIT: TM alloc with baseAddr=%p and mmapSize=%ld, up to %p\n\n", baseAddr, mmapSize, (void*)((uint64_t)baseAddr+(uint64_t)mmapSize));
		printf("Page size: %llu\n", TM_ALLOC_PAGE_SIZE_BYTES);
		printf("Page count: %ld\n", totalPages);
		printf("Page mask: %p\n", (void*)PAGE_MASK);		
		printf("Size of page meta data %ld\n", sizeof(MemoryPage));
		printf("Size of thread data %ld\n", sizeof(ThreadData));
		printf("\n\n");	
    }

	void* getBaseAddr() {		
		return this->baseAddr;
	}

	void* tmMalloc(int tid, size_t size) {
		assert(size >= sizeof(uintptr_t));
		void* ptr = doMalloc(tid, size);
		if (ptr) {
			ThreadData* td = &tData[tid];
			td->allocBufferTail->obj = (Block*)ptr;				
			if (td->allocBufferTail->next) {					
				td->allocBufferTail = td->allocBufferTail->next;
			}
			else {
				BufferNode* newBufferNode = (BufferNode*)malloc(sizeof(BufferNode));					
				newBufferNode->obj = nullptr;
				newBufferNode->next = nullptr;
				td->allocBufferTail->next = newBufferNode;
				td->allocBufferTail = newBufferNode;
			}
			td->allocCount++;
			#ifdef ALLOC_DEBUG2
			printf("Tid: %d requested malloc at %p\n", tid, ptr);
			#endif
			return ptr;
		}
		else {
			printf("Error TM alloc returned nullptr\n");
			exit(-1);
		}
	}

	void tmFree(int tid, void* ptr) {
		ThreadData* td = &tData[tid];

		// td->garbage += *((uintptr_t*)(ptr));		

		#ifdef ALLOC_DEBUG2
		printf("Tid: %d requesting free of ptr %p\n", tid, ptr);
		td->debugFreeCount++;
		#endif
		td->freeBufferTail->obj = (Block*)ptr;
		if (td->freeBufferTail->next) {
			td->freeBufferTail = td->freeBufferTail->next;	
		}
		else {
			BufferNode* newBufferNode = (BufferNode*)malloc(sizeof(BufferNode));					
			newBufferNode->obj = nullptr;
			newBufferNode->next = nullptr;
			td->freeBufferTail->next = newBufferNode;
			td->freeBufferTail = newBufferNode;
		}
		td->freeCount++;				
	}
};

#endif