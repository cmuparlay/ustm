/**
 * A simple insert & delete benchmark for data structures that implement a set.
 */

#include <thread>
#include <cstdlib>
#include <atomic>
#include <string>
#include <cstring>
#include <iostream>

#include "util.h"
#include "trinity.h"
#include "binding.h"

using namespace std;

#define THREAD_COUNT 2
#define TMTYPE trinityvrtl2::persist

#define BIG_ARRAY_SIZE 10000

struct globals_t {
    PaddedRandom rngs[MAX_THREADS];
    volatile char padding0[PADDING_BYTES];
    ElapsedTimer timer;
    volatile char padding1[PADDING_BYTES];
    ElapsedTimer timerFromStart;
    volatile char padding3[PADDING_BYTES];
    // volatile char padding4[PADDING_BYTES];
    volatile bool start;        // used for a custom barrier implementation (should threads start yet?)
    volatile char padding5[PADDING_BYTES];
    atomic_int running;         // used for a custom barrier implementation (how many threads are waiting?)
    volatile char padding6[PADDING_BYTES];
    int totalThreads;
    volatile char padding7[PADDING_BYTES];
    size_t garbage; // garbage variable that will be useful for preventing some code from being optimized out
    volatile char padding8[PADDING_BYTES];
    TMTYPE<int>* bigArray;
    volatile char padding9[PADDING_BYTES];
    
    globals_t(int _totalThreads) {
        for (int i=0;i<MAX_THREADS;++i) {
            rngs[i].setSeed(i+1); // +1 because we don't want thread 0 to get a seed of 0, since seeds of 0 usually mean all random numbers are zero...
        }
        start = false;
        running = 0;
        totalThreads = _totalThreads;
        garbage = -1;
    }
    ~globals_t() {
    }
} __attribute__((aligned(PADDING_BYTES)));

void runTrial(globals_t* g) {
    g->start = false;
    
    // create and start threads
    thread * threads[MAX_THREADS]; // just allocate an array for max threads to avoid changing data layout (which can affect results) when varying thread count. the small amount of wasted space is not a big deal.
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid] = new thread([&, tid]() { /* access all variables by reference, except tid, which we copy (since we don't want our tid to be a reference to the changing loop variable) */
            binding_bindThread(tid);
            size_t garbage = 0; // will prevent contains() calls from being optimized out
            
            // BARRIER WAIT
            g->running.fetch_add(1);
            while (!g->start) { TRACE TPRINT("waiting to start"<<endl); }               // wait to start
                        
            //do stuff
            if (tid == 0) {
                trinityvrtl2::Trinity::updateTx<bool>([&] () {
                    for (uint64_t i = 0; i < BIG_ARRAY_SIZE; i++) {
                        g->bigArray[i] = g->rngs[tid].nextNatural();
                    }
                    return true;
                });
                cout << "TID 0 done" << endl;
            } 
            else {
                trinityvrtl2::Trinity::updateTx<bool>([&] () {
                    for (uint64_t i = BIG_ARRAY_SIZE - 1; i >= 0; i++) {
                        g->bigArray[i] = g->rngs[tid].nextNatural();
                    }
                    return true;
                });

                cout << "TID 1 done" << endl;
            }

            //end
            g->running.fetch_add(-1);
        });
    }
    
    while (g->running < g->totalThreads) {
        TRACE cout<<"main thread: waiting for threads to START running="<<g->running<<endl;
    }
    g->timer.startTimer();    
    __sync_synchronize(); // prevent compiler from reordering "start = true;" before the timer start; this is mostly paranoia, since start is volatile, and nothing should be reordered around volatile reads/writes
    g->start = true; // release all threads from the barrier, so they can work
    
    // sleep the main thread for length of time the trial should run
    // timespec ts;
    // ts.tv_sec = millisToRun / 1000;
    // ts.tv_nsec = 1000000 * (millisToRun % 1000);
    // nanosleep(&ts, NULL);
    
    while (g->running > 0) { std::this_thread::yield(); /* wait for all threads to stop working */ }
    
    
    // join all threads
    for (int tid=0;tid<g->totalThreads;++tid) {
        threads[tid]->join();
        delete threads[tid];
    }
}

void runExperiment(int totalThreads) {
    // create globals struct that all threads will access (with padding to prevent false sharing on control logic meta data)
    auto g = new globals_t(totalThreads);
    
    trinityvrtl2::Trinity::updateTx<bool>([&] () {
        g->bigArray = (TMTYPE<int>*)trinityvrtl2::Trinity::pmalloc(sizeof(TMTYPE<int>) * BIG_ARRAY_SIZE);
        for (uint64_t i = 0; i < BIG_ARRAY_SIZE; i++) {
            g->bigArray[i] = 0;
        }
        return true;
    });

    /**
     * 
     * PREFILL DATA STRUCTURE TO CONTAIN HALF OF THE KEY RANGE
     * (or, if the ratio of insertions to deletions is not 50/50, 
     *  we will prefill the data structure to contain the appropriate fraction
     *  of the key range. for example, with 90% ins, 10% delete,
     *  we prefill to the steady state: 90% full.)
     * (with 0% insert and 0% delete, we prefill to half full.)
     *  
     */
    
    g->timerFromStart.startTimer();
    /**
     * 
     * RUN EXPERIMENT
     * 
     */
    
    cout<<"main thread: experiment starting..."<<endl;
    runTrial(g);
    cout<<"main thread: experiment finished..."<<endl;
    cout<<endl;
    
    /**
     * 
     * PRODUCE OUTPUT
     * 
     */
    
    cout<<endl;
    
    
    if (g->garbage == 0) cout<<endl; // "use" the g->garbage variable, so effectively the return values of all contains() are "used," so they can't be optimized out
    cout<<"total elapsed time="<<(g->timerFromStart.getElapsedMillis()/1000.)<<"s"<<endl;
    delete g;
}

int main(int argc, char** argv) {  
    // configure thread pinning/binding (according to command line args)
    binding_configurePolicy(THREAD_COUNT);
    runExperiment(THREAD_COUNT);
    binding_deinit();

    return 0;
}
