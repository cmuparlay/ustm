
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

#define PM_REGION_SIZE 8*1024*1024*1024ULL 


#include "otherPtms/correia-cx-and-redo/redoopt/RedoOpt.hpp"
#include "TMBTree.hpp"
#include "record_manager.h"

#define DATA_STRUCTURE_T TMBTree<K, V, redoopt::RedoOpt,redoopt::persist>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
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
        redoopt::RedoOpt::updateTx<DATA_STRUCTURE_T*>([&] () {
            ds = redoopt::RedoOpt::tmNew<DATA_STRUCTURE_T>();
            return ds;
        });       
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
        redoopt::RedoOpt::updateTx<DATA_STRUCTURE_T*>([&] () {
            ds = redoopt::RedoOpt::tmNew<DATA_STRUCTURE_T>();
            return ds;
        });

        uint64_t size = 0;
        for (uint64_t ie = 0; ie < expectedSize/100; ie++) {
            redoopt::RedoOpt::updateTx<bool>([&] () {
                for (uint64_t k = 0; k < 100; k++) {
                    ds->add(prefilledKeysArray[size+k], 0);
                    GSTATS_ADD(0, prefill_size, 1);
                }
                return true;
            });
        }
        if (expectedSize%100 != 0) {
            redoopt::RedoOpt::updateTx<bool>([&] () {
                for (uint64_t k = 0; k < expectedSize%100; k++) {
                    ds->add(prefilledKeysArray[size+k], 0);
                    GSTATS_ADD(0, prefill_size, 1);
                }
                return true;
            });
        }

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
        if(ds->remove(key, tid)) {
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
