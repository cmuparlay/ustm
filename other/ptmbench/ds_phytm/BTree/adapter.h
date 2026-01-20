
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"

#define VMEM_POOL_SIZE 3*1024*1024*1024ULL 

#include "phytm2.h"
// #include "TMBTree_phytm2.hpp"
#include "Btree.hpp"

#include "../../ds_tm_common/Btree/common.h"

#define DATA_STRUCTURE_T TMBTree<K, tx_field>

// #define DATA_STRUCTURE_T TMBTree<K, V, tx_field, DEGREE>

template <typename K, typename V, class Reclaim = reclaimer_none<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * ds;

public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_RESERVED,
               const K& unused1,
               const V& VALUE_RESERVED,
               Random64 * const unused2)
    : NO_VALUE(VALUE_RESERVED) {
        printf("Btree: DEGREE = %d\n", DEGREE);        
        
        // TM_INIT();
        // TM_BEGIN_UNSAFE();                  
        // tmInit();      
        // TM_END_UNSAFE();	
        // printf("Done TM Init\n");

        TM_BEGIN_UNSAFE();                          
        // ds = tmNew<DATA_STRUCTURE_T>();                
        ds = tmNew<DATA_STRUCTURE_T>(DEGREE);                
        TM_END_UNSAFE();
    }

	// ds_adapter(const int NUM_THREADS,
	// 			const K &KEY_MIN,
	// 			const K &KEY_MAX,
	// 			const V &VALUE_RESERVED,
	// 			Random64 * const unusedRngs,
	// 			K const * prefilledKeysArray,
	// 			V const * prefilledValsArray,
	// 			int64_t expectedSize,
	// 			int rand)
	// 	: NO_VALUE(VALUE_RESERVED)
	// {				
    //     TM_BEGIN_UNSAFE();  
    //     tmInit();      
    //     printf("Done TM Init\n");
    //     ds = tmNew<DATA_STRUCTURE_T>();        
    //     TM_END_UNSAFE();		
    //     printf("DS alloc done starting prefill.\n");
    //     uint64_t size = 0;
    //     for (uint64_t ie = 0; ie < expectedSize/100; ie++) {
    //         // TM_BEGIN_STM_ONLY();
    //         for (uint64_t k = 0; k < 100; k++) {
    //             ds->add(prefilledKeysArray[size], 0);
    //             GSTATS_ADD(0, prefill_size, 1);
    //             size++;
    //         }                        
    //         // TM_END();
    //         // printf("%ld keys inserted so far.\n", size);
    //     }
    //     // if (expectedSize%100 != 0) {
    //     //     TM_BEGIN();                
    //     //     for (uint64_t k = 0; k < expectedSize%100; k++) {
    //     //         ds->add(prefilledKeysArray[size], prefilledValsArray[size], 0);
    //     //         GSTATS_ADD(0, prefill_size, 1);
    //     //         size++;
    //     //     }                
    //     //     TM_END();
    //     // }
    //     printf("DS prefill done.\n");
    // }

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
        return ds->contains(key, tid);
    }
    V insert(const int tid, const K& key, const V& val) {
        setbench_error("Not implemented");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        if (ds->add(key, tid)) {
            return NO_VALUE;
        }
        return V(1111); //dumb hack
    }
    V erase(const int tid, const K& key) {
        if(!ds->remove(key, tid)) {
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
