
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"

#ifndef USE_ESLOCO
#define USE_ESLOCO
#endif

#ifndef PREFILL_BUILD_FROM_ARRAY
#define PREFILL_BUILD_FROM_ARRAY
#endif

#define PM_FILE_NAME   "/dev/shm/trinitProgressTestFakePMEM"
#define VMEM_FILE_PATH   "/dev/shm/trinitProgressTestVMEM"
#define PM_REGION_SIZE 1*1024*1024*1024ULL 


#include "otherPtms/correia-trinity-and-quadra/trinity2/TrinityVRTL2.hpp"
#include "ArrayTest.h"

#define DATA_STRUCTURE_T ArrayTest<K, trinityvrtl2::persist>

template <typename K, typename V, class Reclaim = reclaimer_none<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    const V NO_VALUE;
    int64_t maxSize;
    int64_t maxSizeOver4;
    DATA_STRUCTURE_T * ds;

public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_RESERVED,
               const K& unused1,
               const V& VALUE_RESERVED,
               Random64 * const unused2)
    : NO_VALUE(VALUE_RESERVED) {
        printf("must use PREFILL_BUILD_FROM_ARRAY\n");
        exit(-1);     
    }

	ds_adapter(const int NUM_THREADS,
				const K &KEY_MIN,
				const K &KEY_MAX,
				const V &VALUE_RESERVED,
				Random64 * const rngs,
				K const * prefilledKeysArray,
				V const * prefilledValsArray,
				int64_t expectedSize,
				int rand)
		: NO_VALUE(VALUE_RESERVED)
	{			
        maxSize = expectedSize*2;	
        maxSizeOver4 = maxSize / 4;
        trinityvrtl2::Trinity::updateTx<DATA_STRUCTURE_T*>([&] () {
            ds = trinityvrtl2::Trinity::tmNew<DATA_STRUCTURE_T>(rngs, maxSize);
            return ds;
        });
    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {        
    }
    void deinitThread(const int tid) {        
    }

    bool contains(const int tid, const K& key) {
        return ds->contains(tid);
    }
    V insert(const int tid, const K& key, const V& val) {
        setbench_error("Not implemented");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {                
        if (ds->insert(tid)) {
            return NO_VALUE;
        }
        return V(1111); //dumb hack
    }
    V erase(const int tid, const K& key) {        
        if(!ds->remove(tid)) {
            return NO_VALUE;
        }
        return V(1111); //dumb hack
    }
    V find(const int tid, const K& key) {
        setbench_error("Not implemented");
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("Not implemented");
    }
    void printSummary() {        
    }
    bool validateStructure() {
        return true;
    }
    static void printObjectSizes() {
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {        
    }
};
