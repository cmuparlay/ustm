#ifndef MALLOC_SWAPPER_H
#define MALLOC_SWAPPER_H

#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>

#include "mmap_alloc.h"

// #define MALLOC_SWAPPER_DEBUG 1

#ifdef MALLOC_SWAPPER_DEBUG
#define MALLOC_SWAPPER_DEBUG_PRINT(format) fprintf(stdout, format);
#else
#define MALLOC_SWAPPER_DEBUG_PRINT(format) 
#endif

#ifdef MALLOC_SWAPPER_DEBUG
	#define MALLOC_SWAPPER_ERROR_PRINT(format) fprintf(stderr, format);
#else
	#define MALLOC_SWAPPER_ERROR_PRINT(format)
#endif




// #define USE_LIBVMMALLOC
// // #define USE_ESLOCO

// #ifdef USE_LIBVMMALLOC 
// 	#define PMALLOC_LIB_SO_PATH "/usr/local/lib/libvmmalloc.so"
// 	#define MALLOC_RECURSES_ONCE	
// #endif

// #ifdef USE_ESLOCO
// 	#define PMALLOC_LIB_SO_PATH "/home/gccoccim/research/nvram_research/persistent-numa-black-box/common/nvm_tools/esloco_lib/esloco.so"
// 	// #undef MALLOC_RECURSES_ONCE
// 	#define MALLOC_RECURSES_ONCE
// #endif

#ifndef likely
    #define likely(x)       __builtin_expect((x),1)
#endif
#ifndef unlikely
    #define unlikely(x)     __builtin_expect((x),0)
#endif


#ifndef BOOL_CAS
#define BOOL_CAS __sync_bool_compare_and_swap
#endif

#ifndef FAA
// __sync_fetch_and_add (type *ptr, type value, ...)
#define FAA __sync_fetch_and_add 
#endif


void init_malloc_wrapper();
void* malloc (size_t) noexcept;
void free();
void* operator new(size_t);
void operator delete(void*);
void* realloc(void *, size_t) noexcept;
void* calloc(size_t, size_t) noexcept;

extern __thread bool tm_alloc_flag;
// extern __thread int tid;

#ifdef MALLOC_RECURSES_ONCE
	__thread long numPmemAlloc = 0;
#endif

char mallocTempBuffer[1024 * 1024]; 
unsigned int mallocTempIndex = 0;

volatile int initializedState = -1;

void* (*oldMallocFunc)(size_t);
void (*oldFreeFunc)(void*);  
void* (*oldReallocFunc)(void*, size_t);
void* (*oldCallocFunc)(size_t, size_t);  



void init_malloc_wrapper() {   
	if (BOOL_CAS(&initializedState, -1, 0)) {		
		*(void **)(&oldMallocFunc) = dlsym(RTLD_NEXT, "malloc");                
		*(void **)(&oldFreeFunc) = dlsym(RTLD_NEXT, "free");		             
		*(void **)(&oldReallocFunc) = dlsym(RTLD_NEXT, "realloc");                
		*(void **)(&oldCallocFunc) = dlsym(RTLD_NEXT, "calloc");		             	
		initializedState = 1;

#ifdef DISABLE_PERSISTENT_MEMORY_USE		
		printf("----------------------------------------------------------\n");		
		printf("----------------------------------------------------------\n");		
		printf("----------------------------------------------------------\n");		
		printf("IMPORTANT PERSISTENT MEMORY USAGE IS EXPLICTLY DISABLED\n");
		printf("Undefine DISABLE_PERSISTENT_MEMORY_USE to re-enable\n");
		printf("----------------------------------------------------------\n");		
		printf("----------------------------------------------------------\n");		
		printf("----------------------------------------------------------\n");		
#endif
	}	
}

void* malloc (size_t size) noexcept {
    if (unlikely(initializedState == -1)) {
        init_malloc_wrapper();
    }

	void* ptr;

	if (unlikely(initializedState == 0)) {		
		if (mallocTempIndex + size < sizeof(mallocTempBuffer)) {			
			int mallocTempIndex_snapshot = mallocTempIndex;
			while (!BOOL_CAS(&mallocTempIndex, mallocTempIndex_snapshot, mallocTempIndex_snapshot + size)) {
				mallocTempIndex_snapshot = mallocTempIndex;
			} 			
			ptr = mallocTempBuffer + mallocTempIndex_snapshot;
			// fprintf(stdout, "%p\n", ptr);	
			// fprintf(stderr, "%d\n", tid);	
			return ptr;
		}
		else {
			// MALLOC_SWAPPER_ERROR_PRINT("TEMP BUFFER FULL\n");
			fprintf(stderr, "Malloc - Allocation on temp buffer failed. Temp buffer full.\n");	
			fprintf(stderr, "Exiting.\n");	
			exit(-1);	
		}
	}

#ifndef DISABLE_PERSISTENT_ALLOCATOR
    if (tm_alloc_flag) {			
		MALLOC_SWAPPER_DEBUG_PRINT("TM Allocation\n");
		ptr = esloco_malloc(size);
		return ptr; 
    }
    else {
		MALLOC_SWAPPER_DEBUG_PRINT("Allocating with system allocator\n");
        ptr = (*oldMallocFunc)(size);
		return ptr;
		// return (*oldMallocFunc)(size);
    }
#else 
	MALLOC_SWAPPER_DEBUG_PRINT("Allocating with system allocator\n");
	ptr = (*oldMallocFunc)(size);
	return ptr;
#endif
    
}

void free (void* ptr) {
	if (ptr == nullptr) {
		MALLOC_SWAPPER_ERROR_PRINT("Free called on nullptr\n");
		return;
	}

	if (ptr >= (void*) mallocTempBuffer && ptr <= (void*)(mallocTempBuffer + mallocTempIndex)) {
		MALLOC_SWAPPER_DEBUG_PRINT("Free called on tmp buffer\n");
		return;
	}

    if (initializedState == -1) {
        MALLOC_SWAPPER_ERROR_PRINT("Called free before malloc\n");
		return;
    }
	else if (initializedState != 1) {
		MALLOC_SWAPPER_DEBUG_PRINT("Free called on tmp buffer\n");
		return;
	}

#ifndef DISABLE_PERSISTENT_ALLOCATOR
    if (tm_alloc_flag) {
        esloco_free(ptr);
    }
    else {
        (*oldFreeFunc)(ptr);
    }
#else 
	(*oldFreeFunc)(ptr);
#endif
}

void* operator new(size_t size) {
    return malloc(size);
}

void operator delete(void* ptr) {
    free(ptr);
}


#endif