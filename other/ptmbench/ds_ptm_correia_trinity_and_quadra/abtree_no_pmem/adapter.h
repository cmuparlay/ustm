/**
 * @TODO: ADD COMMENT HEADER
 */

#pragma once

#include "errors.h"
#include <csignal>
#include <iostream>

#define PM_REGION_SIZE 64*1024*1024*1024ULL 

#define PHTYTM_ENABLE_RANGE_QUERY

#define USE_NOOP_FLUSH_FENCE

#ifndef DISABLE_PMEM
    #define DISABLE_PMEM
#endif

#define ESLOCO_ENABLE_DEFERRED_FREE
#include "otherPtms/correia-trinity-and-quadra/trinity2/TrinityVRTL2.hpp"
#include "abtree_tm.h"

#include "../../ds_tm_common/abtree/common.h"

#define DATA_STRUCTURE_T ABTreeTM<K, V, DEGREE>

template <typename K, typename V, class Reclaim = reclaimer_none<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
  private:
    const void *NO_VALUE;
    DATA_STRUCTURE_T * ds;

  public:
	    ds_adapter(const int NUM_THREADS,
               const K &KEY_ANY,
               const K &KEY_MAX,
               const V &unused2,
               Random64 *const unused3)
    {
#ifndef DISABLE_PMEM
    printf("PMEM ENABLED\n");
#else
    printf("PMEM DISABLED\n");
#endif

#ifndef DISABLE_TM_ALLOCATOR
    printf("TM ALLOCATOR ENABLED\n");
    #ifdef ESLOCO_ENABLE_DEFERRED_FREE
        printf("ESLOCO DEFERRED FREE ENABLED\n");
    #else
        printf("ESLOCO DEFERRED FREE DISABLED\n");
    #endif
#else
    printf("TM ALLOCATOR DISABLED\n");
#endif

        trinityvrtl2::Trinity::updateTx<DATA_STRUCTURE_T*>([&] () {
            ds = trinityvrtl2::Trinity::tmNew<DATA_STRUCTURE_T>(KEY_ANY, KEY_MAX);
            return ds;
        }); 
    }
    
    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return ds->NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread(tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    V insert(const int tid, const K &key, const V &val) {
        setbench_error("insert-replace functionality not implemented for this data structure");
    }

    V insertIfAbsent(const int tid, const K &key, const V &val) {
        V ret = ds->tryInsert(tid, key, val);        
        return ret;
    }

    V erase(const int tid, const K &key) {
        return ds->tryErase(tid, key);
    }

    V find(const int tid, const K &key) {
        return ds->find(tid, key);
    }

    bool contains(const int tid, const K &key) {
        return ds->contains(tid, key);
    }

    int rangeQuery(const int tid, const K &lo, const K &hi, K *const resultKeys, void **const resultValues) {                
#ifndef PHTYTM_ENABLE_RANGE_QUERY        
        setbench_error("unimplemented");
        return -1; //TODO
#else
        int ret;
        trinityvrtl2::Trinity::updateTx<int>([&] () {
            ret = ds->rangeQuery(tid, lo, hi, resultKeys, (void ** const) resultValues);    
            return ret;
        });
        return ret;        
#endif 
    }
    void printSummary() {
        ds->printDebuggingDetails();
    }
    bool validateStructure() {
        printf("validation disabled returning true always\n");
        return true;
        // return ds->validate();
    }

    void printObjectSizes() {
        std::cout << "sizes: node="
                  << (sizeof(Node<K, V, DEGREE>))
                  << std::endl;
    }
};

