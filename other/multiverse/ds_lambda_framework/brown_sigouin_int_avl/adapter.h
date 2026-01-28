/**
 * Copyright Trevor Brown, 2020.
 */

#ifndef BST_ADAPTER_H
#define BST_ADAPTER_H


#include "impl.h"

#define DATA_STRUCTURE_T brown_sigouin_int_avl_tm_auto<K,V,std::less<K>>

template <typename K, typename V, class Reclaim = reclaimer_none<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
  private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * const ds;

  public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_MIN,
               const K& KEY_MAX,
               const V& VALUE_RESERVED,
               Random64 *const unused2)
        : NO_VALUE(VALUE_RESERVED)
        , ds(new DATA_STRUCTURE_T(KEY_MAX, VALUE_RESERVED, NUM_THREADS)) {
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

    void update(const int tid, const K& key, const V& val) {
        #ifdef UPDATERS_USE_INSERT_DELETE
        ds->update(tid, key, val);
        #else
        ds->insert(tid, key, val);
        #endif
    }

    V insert(const int tid, const K &key, const V &val) {
        return ds->insert(tid, key, val);
    }

    V insertIfAbsent(const int tid, const K &key, const V &val) {
        return ds->insertIfAbsent(tid, key, val);
    }

    V erase(const int tid, const K &key) {
        return ds->erase(tid, key);
    }

    V find(const int tid, const K &key) {
        return ds->find(tid, key);
    }

    bool contains(const int tid, const K &key) {
        return ds->find(tid, key) != NO_VALUE;
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        return ds->rangeQuery(lo, hi, resultKeys, (void ** const) resultValues);
    }
    void printSummary() {
    }
    bool validateStructure() {
        return true;
    }

    void printObjectSizes() {
    }

    void debugGCSingleThreaded() {
    }
};

#endif
