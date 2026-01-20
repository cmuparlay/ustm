/**
 * @TODO: ADD COMMENT HEADER
 */

#pragma once

#include "errors.h"
#include <csignal>
#include <iostream>


#define VMEM_POOL_SIZE 8*1024*1024*1024ULL
// #define VMEM_POOL_SIZE 512*1024*1024ULL

// #define DISABLE_TM_ALLOCATOR

#define ESLOCO_ENABLE_DEFERRED_FREE

#define PHTYTM_ENABLE_RANGE_QUERY

#include "tm.h"
#include "abtree.h"

#include "../../ds_tm_common/abtree/common.h"

#ifndef DISABLE_TM_ALLOCATOR
    #define DS_ALLOC tmNew<DATA_STRUCTURE_T>
#else
    #define DS_ALLOC new DATA_STRUCTURE_T
#endif

#define DATA_STRUCTURE_T ABTreeTM<K, V, DEGREE>

template <typename K, typename V, class Reclaim = reclaimer_none<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
  private:
    const void *NO_VALUE;
    DATA_STRUCTURE_T *ds;

  public:
    ds_adapter(const int NUM_THREADS,
               const K &KEY_ANY,
               const K &KEY_MAX,
               const V &unused2,
               Random64 *const unused3)
    {    
        SOFTWARE_BARRIER;
        TM_STARTUP(NUM_THREADS, 0);
        SOFTWARE_BARRIER;

        printf("DONE TM_STARTUP\n");

        TM_THREAD_ENTER();
        TM_BEGIN();                          
        ds = DS_ALLOC(KEY_ANY);        
        TM_END();
        TM_THREAD_EXIT();
    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        // return ds->NO_VALUE;
        return NO_VALUE_ABTREE;
    }

    void initThread(const int tid) {
        TM_THREAD_ENTER();        
    }
    
    void deinitThread(const int tid) {        
        TM_THREAD_EXIT();
    }

    V insert(const int tid, const K &key, const V &val) {
        setbench_error("insert-replace functionality not implemented for this data structure");
    }

    V insertIfAbsent(const int tid, const K &key, const V &val) {
        V ret;
        TM_BEGIN();
        ret = ds->tryInsert(tid, key, val);
        TM_END();
        return ret;
    }

    V erase(const int tid, const K &key) {
        V ret;
        TM_BEGIN();
        ret = ds->tryErase(tid, key);
        TM_END();
        return ret;
    }

    V find(const int tid, const K &key) {
        V ret;
        TM_BEGIN();
        ret = ds->find(tid, key);
        TM_END();
        return ret;
    }

    bool contains(const int tid, const K &key) {
        bool ret;
        TM_BEGIN();
        ret = ds->contains(tid, key);
        TM_END();
        return ret;
    }

    int rangeQuery(const int tid, const K &lo, const K &hi, K *const resultKeys, void **const resultValues) {
#ifndef PHTYTM_ENABLE_RANGE_QUERY        
        setbench_error("unimplemented");
        return -1; //TODO
#else
        int ret;
        TM_BEGIN();
        ret = ds->rangeQuery(tid, lo, hi, resultKeys, (void ** const) resultValues);
        TM_END();
        return ret;        
#endif
    }
    void printSummary() {        
        #warning "SPHT using print summary for TM shutdown ensure this is only called once"           
        TM_SHUTDOWN();                
    }
    bool validateStructure() {
        // printf("validation disabled always returning true\n");
        // return true;        
        printf("starting validation\n");
        bool ret = ds->validate();                
        return ret;
    }

    void printObjectSizes() {
        std::cout << "sizes: node="
                  << (sizeof(Node<K, V, DEGREE>))
                  << std::endl;
    }

    void tmResetCounters() {
        resetCounters();
    }
};

