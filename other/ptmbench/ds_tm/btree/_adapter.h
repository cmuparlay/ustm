
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

// #define HANG_ON_TIME_UP_FAILURE

#include "TMBTree.hpp"
#include "record_manager.h"

#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, Node<K>>
#define DATA_STRUCTURE_T TMBTree<K, V, RECORD_MANAGER_T>

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
        ds = new DATA_STRUCTURE_T(NUM_THREADS);
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
        ds = new DATA_STRUCTURE_T(NUM_THREADS);
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
        return ds->contains(key, tid);
    }
    V insert(const int tid, const K& key, const V& val) {
        setbench_error("Not implemented");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        if (ds->add(key, tid)) {
            return NO_VALUE;
        }
        return V(1); //dumb hack
    }
    V erase(const int tid, const K& key) {
        if(ds->remove(key, tid)) {
            return NO_VALUE;
        }
        return V(1); //dumb hack
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
