/**
 * Setbench test harness for performing rigorous data structure microbenchmarks.
 *
 * Copyright (C) 2018 Trevor Brown
 */

#define MICROBENCH

#include <limits>
#include <cstring>
#include <ctime>
#include <thread>
#include <atomic>
#include <chrono>
#include <cassert>
#include <parallel/algorithm>
#include <omp.h>
#include <perftools.h>


/******************************************************************************
 * Configure global statistics tracking & output using GSTATS (common/gstats)
 * Note: it is crucial that this import occurs before any user headers
 * (that might use GSTATS) are included.
 *
 * This is because it define the macro GSTATS_HANDLE_STATS, which is
 * (read: should be) used by all user includes to determine whether to perform
 * any GSTATS_ calls.
 *
 * Thus, including this before all other user headers ENABLES GSTATS in them.
 *****************************************************************************/
#include "define_global_statistics.h"
#include "gstats_global.h" // include the GSTATS code and macros (crucial this happens after GSTATS_HANDLE_STATS is defined)
// Note: any statistics trackers created by headers included below have to be handled separately... we do this below.
#include "../user_gstats_handler.h"


// each thread saves its own thread-id (should be used primarily within this file--could be eliminated to improve software engineering)
__thread int tid = 0;


// some read-only globals (could be encapsulated in a struct and passed around to improve software engineering)

#include "plaf.h"
PAD;
double TXN_FRAC_CUSTOMER_ADD_NEW_ORDER;
double TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY;
double TXN_FRAC_COMPANY_ADD_ITEM;
double TXN_FRAC_COMPANY_UPDATE_ITEM;
double TXN_FRAC_COMPANY_ANALYTICS;
int MILLIS_TO_RUN;

int RQSIZE;

int NUM_PREFILL_THREADS;
int WORK_THREADS;
int NUM_COMPANY_ANALYTICS_THREADS;
int NUM_REGULAR_THREADS;
int TOTAL_THREADS;

long PREFILL_ITEM_COUNT;
long PREFILL_ORDER_COUNT;

long MAX_ORDER_ID;
long MAX_CUSTOMER_ID;
long MAX_DATE;
long MAX_ORDER_ITEM_COUNT;
long MAX_ITEM_ID;
double ZIPF_PARAM;

long CATEGORY_COUNT;

int COMPANY_THREAD_SLEEP_NANOSECONDS;
PAD;

#include "globals_extern.h"
#include "random_xoshiro256p.h"
// #include "random_fnv1a.h"
#include "plaf.h"
#include "binding.h"
#include "papi_util_impl.h"
#include "keygen.h"

#include "transactionManager.h"

#ifndef INSERT_FUNC
    #define INSERT_FUNC insertIfAbsent
#endif

extern thread_local bool enableStats;
static const long long PREFILL_INTERVAL_MILLIS = 200;


#define THREAD_MEASURED_PRE \
    tid = __tid; \
    binding_bindThread(tid); \
    uint64_t garbage = 0; \
    papi_create_eventset(tid); \
    __sync_fetch_and_add(&g->running, 1); \
    __sync_synchronize(); \
    while (!g->start) { SOFTWARE_BARRIER; TRACE COUTATOMICTID("waiting to start"<<std::endl); } \
    GSTATS_SET(tid, time_thread_start, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count()); \
    papi_start_counters(tid); \
    DURATION_START(tid);

#define THREAD_MEASURED_POST \
    __sync_fetch_and_add(&g->running, -1); \
    DURATION_END(tid, duration_all_ops); \
    GSTATS_SET(tid, time_thread_terminate, std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count()); \
    SOFTWARE_BARRIER; \
    papi_stop_counters(tid); \
    SOFTWARE_BARRIER; \
    while (g->running) { SOFTWARE_BARRIER; } \
    g->txnManager.deinitThread(); \
    g->garbage = g->garbage + garbage;

#define THREAD_PREFILL_PRE \
    tid = __tid; \
    binding_bindThread(tid); \
    uint64_t garbage = 0; \
    __sync_fetch_and_add(&g->running, 1); \
    while (!g->start) { SOFTWARE_BARRIER; TRACE COUTATOMICTID("waiting to start"<<std::endl); } // wait to start

#define THREAD_PREFILL_POST \
    __sync_fetch_and_add(&g->running, -1); \
    SOFTWARE_BARRIER; \
    while (g->running) { SOFTWARE_BARRIER; } \
    g->txnManager.deinitThread(); \
    g->garbage = g->garbage + garbage;




/******************************************************************************
 * Define global variables to store the numerical IDs of all GSTATS global
 * statistics trackers that have been defined over all files #included.
 *
 * It is CRUCIAL that this occurs AFTER ALL user #includes (so we catch ALL
 * GSTATS statistics trackers/counters/timers defined by those #includes).
 *
 * This includes the statistics trackers defined in define_global_statistics.h
 * as well any that were setup by a particular data structure / allocator /
 * reclaimer / pool / library that was #included above.
 *
 * This is a manually constructed list that you are free to add to if you
 * create, e.g., your own data structure specific statistics trackers.
 * They will only be included / printed when your data structure is active.
 *
 * If you add something here, you must also add to a few similar code blocks
 * below. Search this file for "GSTATS_" and you'll see where...
 *****************************************************************************/
GSTATS_DECLARE_ALL_STAT_IDS;
#ifdef GSTATS_HANDLE_STATS_BROWN_EXT_IST_LF
    GSTATS_HANDLE_STATS_BROWN_EXT_IST_LF(__DECLARE_STAT_ID);
#endif
#ifdef GSTATS_HANDLE_STATS_POOL_NUMA
    GSTATS_HANDLE_STATS_POOL_NUMA(__DECLARE_STAT_ID);
#endif
#ifdef GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS
    GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS(__DECLARE_STAT_ID);
#endif
#ifdef GSTATS_HANDLE_STATS_USER
    GSTATS_HANDLE_STATS_USER(__DECLARE_STAT_ID);
#endif
// Create storage for the CONTENTS of gstats counters (for MAX_THREADS_POW2 threads)
GSTATS_DECLARE_STATS_OBJECT(MAX_THREADS_POW2);
// Create storage for the IDs of all global counters defined in define_global_statistics.h



#define TIMING_START(s) \
    std::cout<<"timing_start "<<s<<"..."<<std::endl; \
    GSTATS_TIMER_RESET(tid, timer_duration);
#define TIMING_STOP \
    std::cout<<"timing_elapsed "<<(GSTATS_TIMER_SPLIT(tid, timer_duration)/1000000000.)<<"s"<<std::endl;
#ifndef OPS_BETWEEN_TIME_CHECKS
#define OPS_BETWEEN_TIME_CHECKS 100
#endif
#ifndef RQS_BETWEEN_TIME_CHECKS
#define RQS_BETWEEN_TIME_CHECKS 10
#endif

struct globals_t {    
    PAD;
    // write once
    long elapsedMillis;
    std::chrono::time_point<std::chrono::high_resolution_clock> programExecutionStartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> endTime;
    PAD;
    std::chrono::time_point<std::chrono::high_resolution_clock> startTime;
    long long startClockTicks;
    PAD;
    long elapsedMillisNapping;
    std::chrono::time_point<std::chrono::high_resolution_clock> prefillStartTime;
    PAD;
    volatile uint64_t garbage; // used to prevent optimizing out some code
    PAD;
    // KeyGeneratorUniform<uint64_t>* order_id_prefill_keygens[MAX_THREADS_POW2];
    // KeyGeneratorUniform<uint64_t>* customer_id_prefill_keygens[MAX_THREADS_POW2];
    
    // KeyGeneratorUniform<uint64_t>* order_id_uniform_keygens[MAX_THREADS_POW2];
    // KeyGeneratorUniform<uint64_t>* customer_id_uniform__keygens[MAX_THREADS_POW2];
    // KeyGeneratorUniform<uint64_t>* date_uniform__keygens[MAX_THREADS_POW2];
    #ifdef USE_ZIPF_FAST
    ZipfRejectionInversionSamplerData* keygenZipfData;
    ZipfRejectionInversionSampler* zipf_keygen[MAX_THREADS_POW2];
    #else
    KeyGeneratorZipfData* keygenZipfData;
    KeyGeneratorZipf<uint64_t>* zipf_keygen[MAX_THREADS_POW2];
    #endif
    PAD;
    TransactionManager txnManager;
    PAD;
    Random64 rngs[MAX_THREADS_POW2]; // create per-thread random number generators (padded to avoid false sharing)
//    PAD; // not needed because of padding at the end of rngs
    volatile bool start;
    PAD;
    volatile bool done;
    PAD;
    volatile int running; // number of threads that are running
    PAD;

    globals_t(uint64_t max_order_id, uint64_t max_customer_id, uint64_t max_date) {
        srand(time(0));
        for (int i=0;i<MAX_THREADS_POW2;++i) {
            rngs[i].setSeed(rand());
        }  

        start = false;
        done = false;
        running = 0;    
        garbage = 0;

        #ifdef USE_ZIPF_FAST
        keygenZipfData = new ZipfRejectionInversionSamplerData(max_date);
        #else
        keygenZipfData = new KeyGeneratorZipfData(max_date, ZIPF_PARAM);
        #endif
        
        for (int i=0;i<MAX_THREADS_POW2;++i) {
            // order_id_prefill_keygens[i] = new KeyGeneratorUniform<uint64_t>(&rngs[i], max_order_id);
            // customer_id_prefill_keygens[i] = new KeyGeneratorUniform<uint64_t>(&rngs[i], max_customer_id);
            // date_uniform__keygens[i] = new KeyGeneratorUniform<uint64_t>(&rngs[i], max_customer_id);
            #ifdef USE_ZIPF_FAST
            zipf_keygen[i] = new ZipfRejectionInversionSampler(keygenZipfData, ZIPF_PARAM, &rngs[i]);
            #else
            zipf_keygen[i] = new KeyGeneratorZipf<uint64_t>(keygenZipfData, &rngs[i]);
            #endif
        }                
    }
    ~globals_t() {
        for (int i=0;i<MAX_THREADS_POW2;++i) {
            // delete order_id_prefill_keygens[i];
            // delete customer_id_prefill_keygens[i];
            delete zipf_keygen[i];
        }
        delete keygenZipfData;
    }
};

void regular_work_thread_timed(globals_t* g, int __tid) {
    THREAD_MEASURED_PRE;
    int tid = __tid;
    TIMELINE_START(tid);
    while (!g->done) {
        uint64_t key = 0;
        double op = g->rngs[tid].next(100000000) / 1000000.;
        if (op < TXN_FRAC_CUSTOMER_ADD_NEW_ORDER) {            
            g->txnManager.txn_customerAddNewOrder(tid, &g->rngs[tid]);
            GSTATS_ADD(tid, txn_customer_new_order_txn, 1);
        } else if (op < TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY) {            
            g->txnManager.txn_customerLookupOrderHistoryByDateRange(tid, &g->rngs[tid]);
            GSTATS_ADD(tid, txn_customer_lookup_order_history_over_date_range, 1);
        } else if (op < TXN_FRAC_COMPANY_ADD_ITEM) {                        
            g->txnManager.txn_companyAddNewItem(tid, &g->rngs[tid]);
            GSTATS_ADD(tid, txn_company_add_new_item, 1);
        } else if (op < TXN_FRAC_COMPANY_UPDATE_ITEM) {
            g->txnManager.txn_updateItem(tid, &g->rngs[tid]);
            GSTATS_ADD(tid, txn_company_update_new_item, 1);
        } else if (op < TXN_FRAC_COMPANY_ANALYTICS) {
            uint64_t dateLow = g->zipf_keygen[tid]->next();
            TIMELINE_START(tid);
            garbage += g->txnManager.txn_companySumOrderValueOverDateRange(tid, &g->rngs[tid], dateLow);
            TIMELINE_END(tid, "big_txn");
            GSTATS_ADD(tid, txn_analytics_lookup_order_value_over_date_range, 1);
        } else {            
            // garbage += g->txnManager.txn_multiItemLookup(tid, &g->rngs[tid]);            
            garbage += g->txnManager.txn_singleItemLookup(tid, &g->rngs[tid]);            
            GSTATS_ADD(tid, txn_single_item_lookup, 1);
        }
        GSTATS_ADD(tid, num_operations, 1);
    }
    THREAD_MEASURED_POST;
}


void company_analytics_thread_time(globals_t* g, int __tid) {
    THREAD_MEASURED_PRE;
    enableStats = true;
    while (!g->done) {
        uint64_t dateLow = g->zipf_keygen[tid]->next();
        // printf("big txn from tid: %d\n", tid);

        TIMELINE_START(tid);
        garbage += g->txnManager.txn_companySumOrderValueOverDateRange(tid, &g->rngs[tid], dateLow);
        TIMELINE_END(tid, "big_txn");
        
        // std::this_thread::sleep_for(std::chrono::nanoseconds(COMPANY_THREAD_SLEEP_NANOSECONDS));
        GSTATS_ADD(tid, txn_analytics_lookup_order_value_over_date_range, 1);
        GSTATS_ADD(tid, num_operations, 1);
        
    }
    THREAD_MEASURED_POST;
}

void thread_prefill_items(globals_t* g, int __tid) {
    THREAD_PREFILL_PRE;    
    while (!g->done) {
        if (g->txnManager.txn_companyAddNewItem(tid, &g->rngs[tid])) {
            GSTATS_ADD(tid, prefill_item_size, 1);            
        }
    }    
    THREAD_PREFILL_POST;
}

void thread_prefill_orders(globals_t* g, int __tid) {
    THREAD_PREFILL_PRE;    
    while (!g->done) {        
        if (g->txnManager.txn_customerAddNewOrder(tid, &g->rngs[tid])) {
            GSTATS_ADD(tid, prefill_order_size, 1);
        }        
    }    
    THREAD_PREFILL_POST;
}


void prefill_items(globals_t* g) {
    std::cout<<"-------------------------"<<std::endl;
    std::cout<<"Info: prefilling items."<<std::endl;
    std::chrono::time_point<std::chrono::high_resolution_clock> prefillStartTime = std::chrono::high_resolution_clock::now();

    const double PREFILL_THRESHOLD = 0.02;
    const int MAX_ATTEMPTS = 10000;

    long long totalThreadsPrefillElapsedMillis = 0;

    std::thread * threads[MAX_THREADS_POW2];

    int sz = 0;
    int attempts;
    for (attempts=0;attempts<MAX_ATTEMPTS;++attempts) {
        // start all threads
        for (int i=0;i<NUM_PREFILL_THREADS;++i) {
            threads[i] = new std::thread(thread_prefill_items, g, i);
        }

        TRACE COUTATOMIC("main thread: waiting for threads to START prefilling running="<<g->running<<std::endl);
        while (g->running < NUM_PREFILL_THREADS) {}
        TRACE COUTATOMIC("main thread: starting prefilling timer..."<<std::endl);
        g->startTime = std::chrono::high_resolution_clock::now();

        auto prefillIntervalElapsedMillis = 0;
        __sync_synchronize();
        g->start = true;

        /**
         * START INFINITE LOOP DETECTION CODE
         */
        // amount of time for main thread to wait for children threads
        timespec tsExpected;
        tsExpected.tv_sec = 0;
        tsExpected.tv_nsec = PREFILL_INTERVAL_MILLIS * ((__syscall_slong_t) 1000000);
        // short nap
        timespec tsNap;
        tsNap.tv_sec = 0;
        tsNap.tv_nsec = 200000000; // 200ms

        nanosleep(&tsExpected, NULL);
        g->done = true;
        __sync_synchronize();

        const long MAX_NAPPING_MILLIS = 5000;
        auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count();
        auto elapsedMillisNapping = 0;
        while (g->running > 0 && elapsedMillisNapping < MAX_NAPPING_MILLIS) {
            nanosleep(&tsNap, NULL);
            elapsedMillisNapping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count() - elapsedMillis;
        }
        if (g->running > 0) {
            COUTATOMIC(std::endl);
            COUTATOMIC("Validation FAILURE: "<<g->running<<" non-responsive thread(s) [during prefill]"<<std::endl);
            COUTATOMIC(std::endl);
            #ifndef HANG_ON_TIME_UP_FAILURE
            exit(-1);
            #endif
        }
        /**
         * END INFINITE LOOP DETECTION CODE
         */

        TRACE COUTATOMIC("main thread: waiting for threads to STOP prefilling running="<<g->running<<std::endl);
        while (g->running > 0) {}

        for (int i=0;i<NUM_PREFILL_THREADS;++i) {
            threads[i]->join();
            delete threads[i];
        }

        g->start = false;
        g->done = false;

        sz = GSTATS_OBJECT_NAME.get_sum<long long>(prefill_item_size);
        totalThreadsPrefillElapsedMillis += prefillIntervalElapsedMillis;
        if (sz >= (size_t) PREFILL_ITEM_COUNT*(1-PREFILL_THRESHOLD)) {
            break;
        } else {
            auto currTime = std::chrono::high_resolution_clock::now();
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currTime-prefillStartTime).count();
            // auto szConfirm = g->dsAdapter->size();
            // std::cout << " finished prefilling round "<<attempts<<" with ds size: " << sz << " (CONFIRMING AT "<<szConfirm<<") total elapsed time "<<(totalElapsed/1000.)<<"s"<<std::endl;
            std::cout << " finished prefilling round "<<attempts<<" with ds size: " << sz <<" total elapsed time "<<(totalElapsed/1000.)<<"s"<<std::endl;
            std::cout<<"pref_round_size="<<sz<<std::endl;
        }
    }
    if (attempts >= MAX_ATTEMPTS) {
        std::cerr<<"ERROR: could not prefill to expected size "<<PREFILL_ITEM_COUNT<<". reached size "<<sz<<" after "<<attempts<<" attempts"<<std::endl;
        exit(-1);
    }
    sz = GSTATS_OBJECT_NAME.get_sum<long long>(prefill_item_size);
    std::cout<<"Done prefilling items. size=" << sz <<std::endl;
    std::cout<<"-------------------------"<<std::endl;
}





void prefill_orders(globals_t* g) {
    std::cout<<"-------------------------"<<std::endl;
    std::cout<<"Info: prefilling orders."<<std::endl;
    std::chrono::time_point<std::chrono::high_resolution_clock> prefillStartTime = std::chrono::high_resolution_clock::now();

    const double PREFILL_THRESHOLD = 0.02;
    const int MAX_ATTEMPTS = 10000;

    long long totalThreadsPrefillElapsedMillis = 0;

    std::thread * threads[MAX_THREADS_POW2];

    int sz = 0;
    int attempts;
    for (attempts=0;attempts<MAX_ATTEMPTS;++attempts) {
        // start all threads
        for (int i=0;i<NUM_PREFILL_THREADS;++i) {
            threads[i] = new std::thread(thread_prefill_orders, g, i);
        }

        TRACE COUTATOMIC("main thread: waiting for threads to START prefilling running="<<g->running<<std::endl);
        while (g->running < NUM_PREFILL_THREADS) {}
        TRACE COUTATOMIC("main thread: starting prefilling timer..."<<std::endl);
        g->startTime = std::chrono::high_resolution_clock::now();

        auto prefillIntervalElapsedMillis = 0;
        __sync_synchronize();
        g->start = true;

        /**
         * START INFINITE LOOP DETECTION CODE
         */
        // amount of time for main thread to wait for children threads
        timespec tsExpected;
        tsExpected.tv_sec = 0;
        tsExpected.tv_nsec = PREFILL_INTERVAL_MILLIS * ((__syscall_slong_t) 1000000);
        // short nap
        timespec tsNap;
        tsNap.tv_sec = 0;
        tsNap.tv_nsec = 200000000; // 200ms

        nanosleep(&tsExpected, NULL);
        g->done = true;
        __sync_synchronize();

        const long MAX_NAPPING_MILLIS = 5000;
        auto elapsedMillis = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count();
        auto elapsedMillisNapping = 0;
        while (g->running > 0 && elapsedMillisNapping < MAX_NAPPING_MILLIS) {
            nanosleep(&tsNap, NULL);
            elapsedMillisNapping = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - g->startTime).count() - elapsedMillis;
        }
        if (g->running > 0) {
            COUTATOMIC(std::endl);
            COUTATOMIC("Validation FAILURE: "<<g->running<<" non-responsive thread(s) [during prefill]"<<std::endl);
            COUTATOMIC(std::endl);
            #ifndef HANG_ON_TIME_UP_FAILURE
            exit(-1);
            #endif
        }
        /**
         * END INFINITE LOOP DETECTION CODE
         */

        TRACE COUTATOMIC("main thread: waiting for threads to STOP prefilling running="<<g->running<<std::endl);
        while (g->running > 0) {}

        for (int i=0;i<NUM_PREFILL_THREADS;++i) {
            threads[i]->join();
            delete threads[i];
        }

        g->start = false;
        g->done = false;

        sz = GSTATS_OBJECT_NAME.get_sum<long long>(prefill_order_size);
        totalThreadsPrefillElapsedMillis += prefillIntervalElapsedMillis;
        if (sz >= (size_t) PREFILL_ORDER_COUNT*(1-PREFILL_THRESHOLD)) {
            break;
        } else {
            auto currTime = std::chrono::high_resolution_clock::now();
            auto totalElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(currTime-prefillStartTime).count();
            // auto szConfirm = g->dsAdapter->size();
            // std::cout << " finished prefilling round "<<attempts<<" with ds size: " << sz << " (CONFIRMING AT "<<szConfirm<<") total elapsed time "<<(totalElapsed/1000.)<<"s"<<std::endl;
            std::cout << " finished prefilling round "<<attempts<<" with ds size: " << sz <<" total elapsed time "<<(totalElapsed/1000.)<<"s"<<std::endl;
            std::cout<<"pref_round_size="<<sz<<std::endl;
        }
    }
    if (attempts >= MAX_ATTEMPTS) {
        std::cerr<<"ERROR: could not prefill to expected size "<<PREFILL_ORDER_COUNT<<". reached size "<<sz<<" after "<<attempts<<" attempts"<<std::endl;
        exit(-1);
    }
    std::cout<<"-------------------------"<<std::endl;
}


void createAndPrefillDataStructure(globals_t* g) {
    g->txnManager.init();

    if (NUM_PREFILL_THREADS == 0) {        
        return;
    }

    prefill_items(g);
    g->start = false;
    g->done = false;
    __sync_synchronize();
    std::cout<<"-------------------------"<<std::endl;
    prefill_orders(g);
    g->start = false;
    g->done = false;
    __sync_synchronize();    
    std::cout<<"-------------------------"<<std::endl;    
    GSTATS_CLEAR_ALL;
}

void trial(globals_t* g) {
    using namespace std::chrono;
    papi_init_program(TOTAL_THREADS > NUM_PREFILL_THREADS ? TOTAL_THREADS : NUM_PREFILL_THREADS);

    // create the actual data structure and prefill it to match the expected steady state
    createAndPrefillDataStructure(g);

    // setup measured part of the experiment

    // TODO: reclaim all garbage floating in the record manager that was generated during prefilling, so it doesn't get freed at the start of the measured part of the execution? (maybe it's better not to do this, since it's realistic that there is some floating garbage in the steady state. that said, it's probably not realistic that it's all eligible for reclamation, first thing...)

    // precompute amount of time for main thread to wait for children threads
    timespec tsExpected;
    tsExpected.tv_sec = MILLIS_TO_RUN / 1000;
    tsExpected.tv_nsec = (MILLIS_TO_RUN % 1000) * ((__syscall_slong_t) 1000000);
    // precompute short nap time
    timespec tsNap;
    tsNap.tv_sec = 0;
    tsNap.tv_nsec = 100000000; // 100ms

    // start all threads
    std::thread * threads[MAX_THREADS_POW2];
    for (int i=0;i<NUM_REGULAR_THREADS;++i) {
        if (i < WORK_THREADS) {
            threads[i] = new std::thread(regular_work_thread_timed, g, i);
        } else {
            threads[i] = new std::thread(company_analytics_thread_time, g, i);            
        }
    }

    while (g->running < NUM_REGULAR_THREADS) {
        TRACE COUTATOMIC("main thread: waiting for threads to START running="<<g->running<<std::endl);
    } // wait for all threads to be ready
    COUTATOMIC("main thread: starting timer..."<<std::endl);

    COUTATOMIC(std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC("################################ BEGIN RUNNING ################################"<<std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC(std::endl);

    SOFTWARE_BARRIER;
    g->startTime = std::chrono::high_resolution_clock::now();
    g->startClockTicks = get_server_clock();
    SOFTWARE_BARRIER;
    printUptimeStampForPERF("START");
#ifdef MEASURE_TIMELINE_STATS
    ___timeline_use = 1;
#endif
    g->start = true;
    SOFTWARE_BARRIER;

    // join is replaced with sleeping, and kill threads if they run too long
    // method: sleep for the desired time + a small epsilon,
    //      then check "g->running" to see if we're done.
    //      if not, loop and sleep in small increments for up to 5s,
    //      and exit(-1) if running doesn't hit 0.

    if (MILLIS_TO_RUN > 0) {
        nanosleep(&tsExpected, NULL);
        SOFTWARE_BARRIER;
        g->done = true;
        __sync_synchronize();
        g->endTime = std::chrono::high_resolution_clock::now();
        __sync_synchronize();
        printUptimeStampForPERF("END");
    }

    COUTATOMIC(std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC("################################## TIME IS UP #################################"<<std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC(std::endl);

    const long MAX_NAPPING_MILLIS = 120000;
    g->elapsedMillis = duration_cast<milliseconds>(g->endTime - g->startTime).count();
    g->elapsedMillisNapping = 0;
    while (g->running > 0 && g->elapsedMillisNapping < MAX_NAPPING_MILLIS) {
        nanosleep(&tsNap, NULL);
        g->elapsedMillisNapping = duration_cast<milliseconds>(high_resolution_clock::now() - g->startTime).count() - g->elapsedMillis;
    }

    if (g->running > 0) {
        COUTATOMIC(std::endl);
        COUTATOMIC("Validation FAILURE: "<<g->running<<" non-terminating thread(s) [did we exhaust physical memory and experience excessive slowdown due to swap mem?]"<<std::endl);
        COUTATOMIC(std::endl);
        COUTATOMIC("elapsedMillis="<<g->elapsedMillis<<" elapsedMillisNapping="<<g->elapsedMillisNapping<<std::endl);

        if (g->txnManager.validateAllIndexes()) {
            std::cout<<"Structural validation OK"<<std::endl;
        } else {
            std::cout<<"Structural validation FAILURE."<<std::endl;
        }

        #if defined USE_GSTATS && defined OVERRIDE_PRINT_STATS_ON_ERROR
            GSTATS_PRINT;
            std::cout<<std::endl;
        #endif

        g->txnManager.printAllIndexSummaries();
#ifdef RQ_DEBUGGING_H
        DEBUG_VALIDATE_RQ(NUM_REGULAR_THREADS);
#endif
#ifndef HANG_ON_TIME_UP_FAILURE
        exit(-1);
#endif
    }

    // join all threads
    COUTATOMIC("joining threads...");
    for (int i=0;i<NUM_REGULAR_THREADS;++i) {
        //COUTATOMIC("joining thread "<<i<<std::endl);
        threads[i]->join();
        delete threads[i];
    }

    COUTATOMIC(std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC("################################# END RUNNING #################################"<<std::endl);
    COUTATOMIC("###############################################################################"<<std::endl);
    COUTATOMIC(std::endl);

    COUTATOMIC(((g->elapsedMillis+g->elapsedMillisNapping)/1000.)<<"s"<<std::endl);

    papi_deinit_program();
}

template <class GlobalsT>
void printExecutionTime(GlobalsT * g) {
    auto programExecutionElapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - g->programExecutionStartTime).count();
    std::cout<<"total_execution_walltime="<<(programExecutionElapsed/1000.)<<"s"<<std::endl;
}

void printOutput(globals_t* g) {
    std::cout<<"PRODUCING OUTPUT"<<std::endl;
    g->txnManager.printAllIndexSummaries(); // can put this before GSTATS_PRINT to help some hacky debug code in reclaimer_ebr_token route some information to GSTATS_ to be printed. not a big deal, though.

#ifdef USE_GSTATS
    GSTATS_PRINT;
    std::cout<<std::endl;
#endif

#if !defined SKIP_VALIDATION
    if (g->txnManager.validateAllIndexes()) {
        std::cout<<"Structural validation OK."<<std::endl;
    } else {
        std::cout<<"Structural validation FAILURE."<<std::endl;
        printExecutionTime(g);
        exit(-1);
    }
#endif

    long long totalAll = 0;

    COUTATOMIC("elapsed milliseconds          : "<<g->elapsedMillis<<std::endl);
    COUTATOMIC("napping milliseconds overtime : "<<g->elapsedMillisNapping<<std::endl);
    COUTATOMIC(std::endl);    

    
    const long long totalCustomerOrderHistoryLookup = GSTATS_GET_STAT_METRICS(txn_customer_lookup_order_history_over_date_range, TOTAL)[0].sum;
    const long long totalRandomItemLookup = GSTATS_GET_STAT_METRICS(txn_single_item_lookup, TOTAL)[0].sum;
    const long long totalWorkerReadTxn = totalCustomerOrderHistoryLookup + totalRandomItemLookup;    
    
    const long long totalCompanyAnalyticTxn = GSTATS_GET_STAT_METRICS(txn_analytics_lookup_order_value_over_date_range, TOTAL)[0].sum;

    const long long totalNewOrder = GSTATS_GET_STAT_METRICS(txn_customer_new_order_txn, TOTAL)[0].sum;
    const long long totalNewItem = GSTATS_GET_STAT_METRICS(txn_company_add_new_item, TOTAL)[0].sum;
    const long long totalUpdateItem = GSTATS_GET_STAT_METRICS(txn_company_update_new_item, TOTAL)[0].sum;
    const long long totalItemTableUpdateTxn = totalNewItem + totalUpdateItem;
    const long long totalIndexUpdateTxn = totalNewOrder + totalNewItem + totalUpdateItem;

    const double SECONDS_TO_RUN = (g->elapsedMillis/1000.); // (MILLIS_TO_RUN)/1000.;
    totalAll = totalWorkerReadTxn + totalIndexUpdateTxn + totalCompanyAnalyticTxn;
    const long long totalWorkerThreadTxn = totalWorkerReadTxn + totalIndexUpdateTxn;
    const long long throughputWorkerReadTxn = (long long) (totalWorkerReadTxn / SECONDS_TO_RUN);
    const long long throughputAnalyticsReadTxn = (long long) (totalCompanyAnalyticTxn / SECONDS_TO_RUN);
    
    const long long throughputUpdateTxn = (long long) (totalIndexUpdateTxn / SECONDS_TO_RUN);
    const long long throughputAll = (long long) (totalAll / SECONDS_TO_RUN);
    const long long throughputWorkerThreads = (long long) (totalWorkerThreadTxn / SECONDS_TO_RUN);
    const long long throughputAnalyticThreads = throughputAnalyticsReadTxn;

    COUTATOMIC(std::endl);
    COUTATOMIC("total_new_order_txn="<<totalNewOrder<<std::endl);
    COUTATOMIC("total_new_item_txn="<<totalNewItem<<std::endl);
    COUTATOMIC("total_update_item_txn="<<totalUpdateItem<<std::endl);
    COUTATOMIC("total_random_items_lookup_txn="<<totalRandomItemLookup<<std::endl);
    COUTATOMIC("total_customer_order_history_lookup_txn="<<totalCustomerOrderHistoryLookup<<std::endl);
    COUTATOMIC("total_company_analytics_txn="<<totalCompanyAnalyticTxn<<std::endl);
    COUTATOMIC("total_updates="<<totalIndexUpdateTxn<<std::endl);
    COUTATOMIC("total_worker_reads="<<totalWorkerReadTxn<<std::endl);
    COUTATOMIC("total_analytic_reads="<<totalCompanyAnalyticTxn<<std::endl);

    COUTATOMIC("worker_thread_update_throughput="<<throughputUpdateTxn<<std::endl);
    COUTATOMIC("worker_thread_query_throughput="<<throughputWorkerReadTxn<<std::endl);
    COUTATOMIC("analytic_thread_query_throughput="<<throughputAnalyticsReadTxn<<std::endl);
    COUTATOMIC("worker_thread_total_throughput="<<throughputWorkerThreads<<std::endl);
    COUTATOMIC("total_throughput="<<throughputAll<<std::endl);
    COUTATOMIC(std::endl);

    COUTATOMIC(std::endl);
    COUTATOMIC("total new_order_txn                         : "<<totalNewOrder<<std::endl);
    COUTATOMIC("total new_item_txn                          : "<<totalNewItem<<std::endl);
    COUTATOMIC("total update_item_txn                       : "<<totalUpdateItem<<std::endl);
    COUTATOMIC("total random_items_lookup_txn               : "<<totalRandomItemLookup<<std::endl);
    COUTATOMIC("total customer_order_history_lookup_txn     : "<<totalCustomerOrderHistoryLookup<<std::endl);
    COUTATOMIC("total company_analytics_txn                 : "<<totalCompanyAnalyticTxn<<std::endl);
    COUTATOMIC("total updates                               : "<<totalIndexUpdateTxn<<std::endl);
    COUTATOMIC("total worker_reads                          : "<<totalWorkerReadTxn<<std::endl);
    COUTATOMIC("total analytic_reads                        : "<<totalCompanyAnalyticTxn<<std::endl);
    COUTATOMIC("total txn                                   : "<<totalAll<<std::endl);

    COUTATOMIC("throughput worker_thread_update             : "<<throughputUpdateTxn<<std::endl);
    COUTATOMIC("throughput worker_thread_query              : "<<throughputWorkerReadTxn<<std::endl);
    COUTATOMIC("throughput analytic_thread_query            : "<<throughputAnalyticsReadTxn<<std::endl);
    COUTATOMIC("total_throughput worker_thread              : "<<throughputWorkerThreads<<std::endl);
    COUTATOMIC("total_throughput all_threads                : "<<throughputAll<<std::endl);
    COUTATOMIC(std::endl);

papi_print_counters(totalAll);

#ifdef NO_OPTIMIZE_IS_ON
    std::cout<<"WARNING: THIS BINARY WAS COMPILED WITH NO OPTMIZATIONS. This is not appropriate for experiment results."<<std::endl;
#endif
#if !defined NDEBUG
    std::cout<<"WARNING: NDEBUG is not defined, so experiment results may be affected by assertions and debug code."<<std::endl;
#endif
#if defined MEASURE_REBUILDING_TIME || defined MEASURE_TIMELINE_STATS || defined RAPID_RECLAMATION
    std::cout<<"WARNING: one or more of MEASURE_REBUILDING_TIME | MEASURE_TIMELINE_STATS | RAPID_RECLAMATION are defined, which *may* affect experiments results."<<std::endl;
#endif
}

void main_continued_with_globals(globals_t* g) {
    g->programExecutionStartTime = std::chrono::high_resolution_clock::now();
    
    // setup thread pinning/binding
    binding_configurePolicy(TOTAL_THREADS);

    // print actual thread pinning/binding layout
    std::cout<<"ACTUAL_THREAD_BINDINGS=";
    for (int i=0;i<TOTAL_THREADS;++i) {
        std::cout<<(i?",":"")<<binding_getActualBinding(i);
    }
    std::cout<<std::endl;
    if (!binding_isInjectiveMapping(TOTAL_THREADS)) {
        std::cout<<"ERROR: thread binding maps more than one thread to a single logical processor"<<std::endl;
        exit(-1);
    }

    /******************************************************************************
     * Perform the actual creation of all GSTATS global statistics trackers that
     * have been defined over all files #included.
     *
     * This includes the statistics trackers defined in define_global_statistics.h
     * as well any that were setup by a particular data structure / allocator /
     * reclaimer / pool / library that was #included above.
     *
     * This is a manually constructed list that you are free to add to if you
     * create, e.g., your own data structure specific statistics trackers.
     * They will only be included / printed when your data structure is active.
     *****************************************************************************/
    std::cout<<std::endl;
    #ifdef GSTATS_HANDLE_STATS_BROWN_EXT_IST_LF
        GSTATS_HANDLE_STATS_BROWN_EXT_IST_LF(__CREATE_STAT);
    #endif
    #ifdef GSTATS_HANDLE_STATS_POOL_NUMA
        GSTATS_HANDLE_STATS_POOL_NUMA(__CREATE_STAT);
    #endif
    #ifdef GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS
        GSTATS_HANDLE_STATS_RECLAIMERS_WITH_EPOCHS(__CREATE_STAT);
    #endif
    #ifdef GSTATS_HANDLE_STATS_USER
        GSTATS_HANDLE_STATS_USER(__CREATE_STAT);
    #endif
    GSTATS_CREATE_ALL;
    std::cout<<std::endl;

    trial(g);
    printOutput(g);

    binding_deinit();
    std::cout<<"garbage="<<g->garbage<<std::endl; // to prevent certain steps from being optimized out
    GSTATS_DESTROY;

    printExecutionTime(g);
#ifndef DISABLE_GLOBALS_DEALLOCATION
    delete g;
#endif
}

int parseCommaSeparatedTokenStringToLongArray(std::string tokenString, long* arr, int maxArrSize) {        
    std::stringstream ss;
    ss << tokenString;
    int index = 0;
    while(ss.good()) {
        std::string token;
        getline( ss, token, ',' );
        arr[index] = std::stol(token);
        index++;
        if (index >= maxArrSize) {
            printf("Error, token string contains more than the intended number of tokens or more than the maximum number of tokens\n");
            exit(-1);
        }
    }

    return index;
}

int parseCommaSeparatedTokenStringToIntArray(std::string tokenString, int* arr, int maxArrSize) {        
    std::stringstream ss;
    ss << tokenString;
    int index = 0;
    while(ss.good()) {
        std::string token;
        getline( ss, token, ',' );
        arr[index] = std::stoi(token);
        index++;
        if (index >= maxArrSize) {
            printf("Error, token string contains more than the intended number of tokens or more than the maximum number of tokens\n");
            exit(-1);
        }
    }

    return index;
}


int main(int argc, char** argv) {
    printUptimeStampForPERF("MAIN_START");
    if (argc < 19) {
        std::cout << "missing args" << std::endl;
        std::cout << "Example usage" << std::endl;
        std::cout << "./bin/db_multiverse -nprefill 128 -n 126 -nca 2 -rqsize 64 -zipf 0.1 -cats 1000 -kcategories 100000 -kcustomers 10000000 -kitems 10000000 -korders 10000000 -kdates 500 -max-items-per-order 100  -prefillitems 1000000 -prefillorders 1000000 -txn_ao 0.4 -txn_lod 0.2 -txn_ai 0.1 -txn_ui 0.2 -t 20000 -pin 64-127,192-255,0-63,128-191" << std::endl;
        return 1;
    }

    std::cout<<"binary="<<argv[0]<<std::endl;

    std::cout << std::endl;
    std::cout << TM_NAME << std::endl;
    std::cout << std::endl;

    // setup default args    
    TXN_FRAC_CUSTOMER_ADD_NEW_ORDER = 0;
    TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY = 0;
    TXN_FRAC_COMPANY_ADD_ITEM = 0;
    TXN_FRAC_COMPANY_UPDATE_ITEM = 0;
    TXN_FRAC_COMPANY_ANALYTICS = 0;

    RQSIZE = 0;

    NUM_PREFILL_THREADS = 0;
    NUM_COMPANY_ANALYTICS_THREADS = 0;
    WORK_THREADS = 4;        
    COMPANY_THREAD_SLEEP_NANOSECONDS = 0;
    
    MILLIS_TO_RUN = 1000;

    // read command line args
    for (int i=1;i<argc;++i) {
        if (strcmp(argv[i], "-t") == 0) {
            MILLIS_TO_RUN = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-n") == 0) {
            WORK_THREADS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-rqsize") == 0) {
            RQSIZE = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-nca") == 0) {
            NUM_COMPANY_ANALYTICS_THREADS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-cats") == 0) {
            COMPANY_THREAD_SLEEP_NANOSECONDS = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-kcategories") == 0) {
            CATEGORY_COUNT = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-kitems") == 0) {
            MAX_ITEM_ID = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-kdates") == 0) {
            MAX_DATE = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-korders") == 0) {
            MAX_ORDER_ID = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-kcustomers") == 0) {
            MAX_CUSTOMER_ID = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-max-items-per-order") == 0) {
            MAX_ORDER_ITEM_COUNT = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-zipf") == 0) {
            ZIPF_PARAM = atof(argv[++i]);
        } else if (strcmp(argv[i], "-nprefill") == 0) {
            NUM_PREFILL_THREADS = atoi(argv[++i]);        
        } else if (strcmp(argv[i], "-prefillitems") == 0) {
            PREFILL_ITEM_COUNT = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-prefillorders") == 0) {
            PREFILL_ORDER_COUNT = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-txn-add-order") == 0) {
            TXN_FRAC_CUSTOMER_ADD_NEW_ORDER = atof(argv[++i]);
        } else if (strcmp(argv[i], "-txn-customer-history-lookup") == 0) {
            TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY = atof(argv[++i]);
        } else if (strcmp(argv[i], "-txn-add-item") == 0) {
            TXN_FRAC_COMPANY_ADD_ITEM = atof(argv[++i]);
        } else if (strcmp(argv[i], "-txn-update-item") == 0) {
            TXN_FRAC_COMPANY_UPDATE_ITEM = atof(argv[++i]);
        } else if (strcmp(argv[i], "-txn-company-analytics") == 0) {
            TXN_FRAC_COMPANY_ANALYTICS = atof(argv[++i]);
        } else if (strcmp(argv[i], "-pin") == 0) { // e.g., "-pin 1.2.3.8-11.4-7.0"
            binding_parseCustom(argv[++i]); // e.g., "1.2.3.8-11.4-7.0"
            std::cout<<"parsed custom binding: "<<argv[i]<<std::endl;
        } else {
            std::cout<<"bad argument "<<argv[i]<<std::endl;
            exit(1);
        }
    }
    TOTAL_THREADS = WORK_THREADS + NUM_COMPANY_ANALYTICS_THREADS;
    NUM_REGULAR_THREADS = WORK_THREADS + NUM_COMPANY_ANALYTICS_THREADS;
    TOTAL_THREADS = NUM_REGULAR_THREADS > NUM_PREFILL_THREADS ? NUM_REGULAR_THREADS : NUM_PREFILL_THREADS;    

    // print used args
    PRINTS(DS_TYPENAME);
    PRINTS(MAX_THREADS_POW2);
    PRINTS(CPU_FREQ_GHZ);
    
    printf("\n");
    PRINTI(RQSIZE);
    PRINTI(MILLIS_TO_RUN);
    PRINTI(WORK_THREADS);
    PRINTI(NUM_COMPANY_ANALYTICS_THREADS);
    PRINTI(NUM_PREFILL_THREADS);
    PRINTI(NUM_REGULAR_THREADS);
    
    printf("\n");
    PRINTI(COMPANY_THREAD_SLEEP_NANOSECONDS);
    PRINTI(CATEGORY_COUNT);
    PRINTI(PREFILL_ITEM_COUNT);
    PRINTI(PREFILL_ORDER_COUNT);

    PRINTI(MAX_ORDER_ID);
    PRINTI(MAX_CUSTOMER_ID);
    PRINTI(MAX_DATE);
    PRINTI(MAX_ORDER_ITEM_COUNT);
    PRINTI(MAX_ITEM_ID);

    PRINTI(TXN_FRAC_CUSTOMER_ADD_NEW_ORDER);
    PRINTI(TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY);
    PRINTI(TXN_FRAC_COMPANY_ADD_ITEM);
    PRINTI(TXN_FRAC_COMPANY_UPDATE_ITEM);
    printf("\n");

    //just so we dont need to do this in the thread operation loop
    TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY += TXN_FRAC_CUSTOMER_ADD_NEW_ORDER;
    TXN_FRAC_COMPANY_ADD_ITEM += TXN_FRAC_CUSTOMER_LOOKUP_ORDER_HISTORY;
    TXN_FRAC_COMPANY_UPDATE_ITEM += TXN_FRAC_COMPANY_ADD_ITEM;
    TXN_FRAC_COMPANY_ANALYTICS += TXN_FRAC_COMPANY_UPDATE_ITEM;

    main_continued_with_globals(new globals_t(MAX_ORDER_ID, MAX_CUSTOMER_ID, MAX_DATE));
    
    printUptimeStampForPERF("MAIN_END");
    return 0;
}
