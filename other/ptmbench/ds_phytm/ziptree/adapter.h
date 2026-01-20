
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

#include "phytm2.h"
#include "TMZipTreeSet.hpp"

#define DATA_STRUCTURE_T TMZipTreeSet<K, tx_field>

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
        TM_INIT();
        TM_BEGIN_UNSAFE();                  
        tmInit();      
        TM_END_UNSAFE();	
        printf("Done TM Init\n");
        
        TM_BEGIN_UNSAFE();          
        ds = tmNew<DATA_STRUCTURE_T>();        
        TM_END_UNSAFE();      
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
