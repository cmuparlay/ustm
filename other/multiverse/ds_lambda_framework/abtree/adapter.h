#pragma once

#include "../tm.h"
#include "abtree.hpp"

#define DATA_STRUCTURE_T ABTreeTM<K, V>

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
        ds = new DATA_STRUCTURE_T();
    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void update(const int tid, const K& key, const V& val) {
        #ifdef UPDATERS_USE_INSERT_DELETE
        ds->update_insert_delete(key, val);
        #else
        ds->update(key, val);
        #endif
    }

    void initThread(const int tid) {
    }
    void deinitThread(const int tid) {
        ds->deinitThread();
    }

    bool contains(const int tid, const K& key) {
        return ds->contains(key);
    }
    V insert(const int tid, const K& key, const V& val) {
        setbench_error("Not implemented");
    }
    V insertIfAbsent(const int& tid, const K& key, const V& val) {        
        return ds->addKey(key, val);
    }
    V erase(const int& tid, const K& key) {        
        return ds->removeKey(key);
    }
    V find(const int tid, const K& key) {
        setbench_error("Not implemented");
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        int rqResultSize = 0;
        // TM_T::template readTx<bool> ([&] {
        //     rqResultSize = ds->rangeQuery(lo, hi, resultKeys, (void ** const) resultValues);
        // });

        rqResultSize = ds->rangeQuery(lo, hi, resultKeys, (void ** const) resultValues);

        return rqResultSize;
    }
    void printSummary() {        
    }
    bool validateStructure() {
        printf("\n\nWARNING VALIDATION FOR THIS DATA STRUCTURE IS DISABLED\n\n");
        return true;
        // bool ret;
        // TM_T::template readTx<bool> ([&] {
        //     ret = ds->validate(0);
        // });
        // return ret;
    }
    static void printObjectSizes() {
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {        
    }
};
