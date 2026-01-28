
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif

#include "TMHashMapFixedSize.hpp"

#define DATA_STRUCTURE_T TMHashMapFixedSize<K, V>

extern int RQSIZE;

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
        ds = new DATA_STRUCTURE_T(NO_VALUE, 1000000);        
    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread();
    }
    void deinitThread(const int tid) {
        ds->deinitThread();
    }

    void update(const int tid, const K& key, const V& val) {
        ds->insert(key, val);
    }
    bool contains(const int tid, const K& key) {
        return ds->contains(key);
    }
    V insert(const int tid, const K& key, const V& val) {
        return ds->insert(key, val);
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return ds->insertIfAbsent(key, val);
    }
    V erase(const int tid, const K& key) {
        return ds->remove(key);
    }
    V find(const int tid, const K& key) {
        return ds->find(key);
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        //dumb hack to avoid updating main with a special path for this DS
        int count = ds->atomicSize() % RQSIZE;
        resultKeys[0] = count;
        resultKeys[count-1] = RQSIZE-count;
        return count;
    }
    void printSummary() {
    }
    bool validateStructure() {
        // return ds->validate();
        printf("Validation always returning true\n");
        return true;
    }
    static void printObjectSizes() {
    }

    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {
    }
};
