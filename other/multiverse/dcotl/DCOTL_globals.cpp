// Copied globals from the DCOTL header -> can use this in macrobenchmark 
//  or any compilation target that needs to include the TM header multiple times
#include <atomic>
#include "DCOTL.hpp"

#warning DCOTL_globals.cpp -> ensure that this file contains the correct and complete set of globals from the TM header

namespace ns_dcotl {
    ThreadRegistry gThreadRegistry {};

    Globals globals {};

    DCOTL gDCOTL {};

    MemoryManager gMemManager {};

    thread_local AllocInfo* tl_allocInfo {nullptr};


    // Thread-local data of the current ongoing transaction
    thread_local OpData* tl_opdata {nullptr};
    // This is where every thread stores the tid it has been assigned when it calls getTID() for the first time.
    // When the thread dies, the destructor of ThreadCheckInCheckOut will be called and de-register the thread.
    thread_local ThreadCheckInCheckOut tl_tcico {};
    // Helper function for thread de-registration
    void thread_registry_deregister_thread(const int tid) {
        gThreadRegistry.deregister_thread(tid);
    }
    void thread_registry_deregister_thread() {
        const int tid = ThreadRegistry::getTID();
        gThreadRegistry.deregister_thread(tid);
    }

    [[noreturn]] void abortTx(OpData* myd) {
        myd->writeSet.rollback(myd->tid);
        uint64_t nextClock = globals.gClock.fetch_add(1);
        // printf("aborted, nextclock = %ld\n", nextClock);
        STATS_LIST_APPEND(myd->tid, rdset_size_at_abort, myd->readSet.size);
        STATS_LIST_APPEND(myd->tid, wrset_size_at_abort, myd->writeSet.size);

        gMemManager.abort(myd->tid);

        // Unlock with the new sequence
        myd->writeSet.unlock(nextClock, myd->tid);
        #ifdef TRACK_REGULAR_METRICS
        STATS_COUNTER_ADD(myd->tid, aborts, 1);
        #endif

        if (myd->tx_type == TX_IS_READ) {
            STATS_COUNTER_ADD(myd->tid, read_only_aborts, 1);
        }

        // if (myd->tx_type == TX_IS_READ && myd->attempt == DCTL_K_FAILS) {
        if (myd->attempt == DCTL_K_FAILS) {
            myd->tx_type = TX_IS_IRREVOCABLE;
            STATS_COUNTER_ADD(myd->tid, irrevocable_from_aborts, 1);

            TM_TIMELINE_START((int)myd->tid);
        }

        #ifdef DEBUG_TRACK_TXN_DURATION
        myd->total_regular_txn_attempt_ms_time_at_abort += (get_server_clock() - myd->txn_begin_time ) / 1000000.0;
        myd->num_regular_txn_abort++;
        // myd->total_regular_txn_attempt_ms_time_at_abort += 13;
        // uint64_t x = 10;
        // uint64_t y = 11;
        #endif

        std::longjmp(myd->env, 1);
    }
}