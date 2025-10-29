
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

#ifndef USE_ESLOCO
#define USE_ESLOCO
#endif

#define PM_FILE_NAME   "/mnt/pmem1_mount/trinity_vr_tl2_guy"
#define PM_REGION_SIZE 8*1024*1024*1024ULL 

#include "phytm2.h"
#include "ext_bst_tm.h"

#define DATA_STRUCTURE_T ext_bst_tm<K, V, std::less<K>>

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
    : NO_VALUE(VALUE_RESERVED)
    {                
        ds = new DATA_STRUCTURE_T(KEY_RESERVED, NO_VALUE, NUM_THREADS);
    }	
 
	ds_adapter(const int NUM_THREADS,
				const K &KEY_MIN,
				const K &KEY_MAX,
				const V &VALUE_RESERVED,
				Random64 * const unusedRngs,
				K const * prefilledKeysArray,
				V const * prefilledValsArray,
				int64_t expectedSize,
				int rand)
		: NO_VALUE(VALUE_RESERVED)
	{		
        TM_BEGIN();  
        tmInit();      
        printf("Done TM Init\n");
        ds = tmNew<DATA_STRUCTURE_T>(KEY_MIN, NO_VALUE, NUM_THREADS);        
        TM_END();		

        printf("DS alloc done starting prefill.\n");

        uint64_t size = 0;
        for (uint64_t ie = 0; ie < expectedSize/100; ie++) {
            // TM_BEGIN_STM_ONLY();
            for (uint64_t k = 0; k < 100; k++) {
                ds->insertIfAbsent(0, prefilledKeysArray[size+k], prefilledValsArray[size+k]);
                GSTATS_ADD(0, prefill_size, 1);
                size++;
            }            
            printf("inserted 100 more keys\n");
            // TM_END();
        }
        // if (expectedSize%100 != 0) {
        //     TM_BEGIN();                
        //     for (uint64_t k = 0; k < expectedSize%100; k++) {
        //         ds->add(prefilledKeysArray[size+k], prefilledValsArray[size+k], 0);
        //         GSTATS_ADD(0, prefill_size, 1);
        //     }                
        //     TM_END();
        // }
        printf("DS prefill done.\n");
    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread(tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    bool contains(const int tid, const K& key) {
        return ds->contains(tid, key);
    }
    V insert(const int tid, const K& key, const V& val) {
        // return ds->insert(tid, key, val);
        setbench_error("No implemented\n");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return ds->insertIfAbsent(tid, key, val);
    }
    V erase(const int tid, const K& key) {
        return ds->erase(tid, key);
    }
    V find(const int tid, const K& key) {
        return ds->find(tid, key);
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        // return ds->rangeUpdate(tid, lo, hi);        
        setbench_error("No implemented\n");
    }
    void printSummary() {
        // auto recmgr = ds->debugGetRecMgr();
        // recmgr->printStatus();
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
