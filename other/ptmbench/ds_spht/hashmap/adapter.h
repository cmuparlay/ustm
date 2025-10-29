
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"

#include "tm.h"
#include "TMHashMapFixedSize.hpp"

#define DATA_STRUCTURE_T TMHashMapFixedSize<K, V, tx_field>

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
        TM_STARTUP(NUM_THREADS, 0);

        // for (int i = 0; i < NUM_THREADS; i++) {
        //     TM_THREAD_ENTER(i);
        // }
        TM_THREAD_ENTER();
        TM_BEGIN();          
        ds = tmNew<DATA_STRUCTURE_T>();        
        TM_END();
        TM_THREAD_EXIT();
    }

    ~ds_adapter() {        
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {        
        TM_THREAD_ENTER();
    }
    void deinitThread(const int tid) {        
        TM_THREAD_EXIT();
    }

    bool contains(const int tid, const K& key) {
        bool ret;
        TM_BEGIN();
        ret = ds->contains(key);
        TM_END();
        return ret;
    }
    V insert(const int tid, const K& key, const V& val) {
        setbench_error("Not implemented");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        V ret;
        TM_BEGIN();
        if (ds->add(key, val)) {
            ret = NO_VALUE;
        }
        else {
            ret = V(1111); //dumb hack
        } 
        TM_END();
        return ret;
    }
    V erase(const int tid, const K& key) {
        V ret;
        TM_BEGIN();
        if(!ds->remove(key)) {
            ret = NO_VALUE;
        }
        else {
            ret = V(1111); //dumb hack
        }        
        TM_END();
        return ret;
    }
    V find(const int tid, const K& key) {
        setbench_error("Not implemented");
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("Not implemented");
    }
    void printSummary() {     
        #warning "SPHT using print summary for TM shutdown ensure this is only called once"   
        TM_SHUTDOWN();
    }
    bool validateStructure() {        
        printf("WARNING: validation has no been implented for this DS. Always returning true.");
        return true;
    }    
    static void printObjectSizes() {
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {        
    }

    void tmResetCounters() {
        resetCounters();
    }
};
