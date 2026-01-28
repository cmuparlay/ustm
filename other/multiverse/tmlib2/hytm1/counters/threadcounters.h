/*
 * File:   theadcounters.h
 * Author: trbot
 *
 * Created on February 4, 2017, 2:19 AM
 */

#ifndef THEADCOUNTERS_H
#define	THEADCOUNTERS_H

/**
 * SEE BOTTOM FOR STATIC GLOBALS
 */

#include <time.h>
#include <string>
#include <sstream>
#include <iostream>
#include "../platform.h"
using namespace std;

// #define MAX_ABORT_STATUS 4096
#define MAX_ABORT_STATUS 8

#define NUMBER_OF_PATHS 3
#define PATH_FAST_HTM 0
#define PATH_SLOW_HTM 1
#define PATH_FALLBACK 2

typedef long long tm_counter_t;

#ifdef RECORD_ABORT_ADDRESSES
#define MAX_ABORT_ADDR (1<<20)
int numAbortAddr = 0; // for thread 0
int numAbortAddrExact = 0; // for thread 0
long abortAddr[MAX_ABORT_ADDR];
#endif

#define __TM_COUNTER_GET(name) (counters[tid]->name)
#define __TM_COUNTER_SET(name, val) (counters[tid]->name = (val))
#define __TM_COUNTER_ADD(name, val) (counters[tid]->name += (val))
#define __TM_COUNTER_TOTAL(name, nthreads) ({ long long ___tm_totalsum=0; for (int ___tm_tid=0;___tm_tid<(nthreads);++___tm_tid) { ___tm_totalsum += counters[___tm_tid]->name; } ___tm_totalsum; })

#define TM_COUNTER_INC(name, tid) (++__tm_counters[(tid)]->name)
#define TM_COUNTER_SET(name, val, tid) (__tm_counters[(tid)]->name = (val))
#define TM_COUNTER_ADD(name, val, tid) (__tm_counters[(tid)]->name += (val))
#define TM_COUNTER_TOTAL(name, nthreads) ({ long long ___tm_totalsum=0; for (int ___tm_tid=0;___tm_tid<(nthreads);++___tm_tid) { ___tm_totalsum += __tm_counters[___tm_tid]->name; } ___tm_totalsum; })
#define TM_REGISTER_ABORT(path, xarg, tid) registerHTMAbort(__tm_counters, (tid), (xarg), (path))
#define TM_TIMER_START(tid) countersProbStartTime(__tm_counters, (tid), 0.)
#define TM_TIMER_END(tid) countersProbEndTime(__tm_counters, (tid) /*, __tm_counters[(tid)]->timingOnFallback*/)
#define TM_CLEAR_COUNTERS() __tm_counters.clear()
#define TM_PRINT_COUNTERS() countersPrint(__tm_counters)
#define TM_CREATE_COUNTERS()
#define TM_DESTROY_COUNTERS()

class tm_threadCounters {
public:
    char padding0[PREFETCH_SIZE_BYTES];
#ifdef RECORD_ABORTS
    tm_counter_t htmAbort[NUMBER_OF_PATHS*MAX_ABORT_STATUS];
#else
    tm_counter_t htmAbort[NUMBER_OF_PATHS];
#endif
    tm_counter_t htmCommit[NUMBER_OF_PATHS];
    tm_counter_t timingTemp;            // per process timestamps: 0 if not currently timing, o/w > 0
    tm_counter_t timingOnFallback;      // per process total DURATIONS over execution (scaled down by the probability of timing on a countersProbStartTime call)
//    tm_counter_t jumpToSlow;
    tm_counter_t garbage;
    char padding1[PREFETCH_SIZE_BYTES];

    void clear() {
        memset(this, 0, sizeof(*this));
    }
    tm_threadCounters() {
        clear();
    }
} __attribute__((aligned(PREFETCH_SIZE_BYTES)));

template <int NUM_PROCESSES>
class tm_debugCounters {
    // no padding needed -- implied by below
    tm_threadCounters counters[NUM_PROCESSES];
    // no padding needed -- implied by above
public:
    tm_threadCounters * operator[](int tid) {
        return counters+tid;
    }
    void clear() {
        for (int i=0;i<NUM_PROCESSES;++i) {
            counters[i].clear();
        }
    }
    tm_debugCounters() {
        clear();
    }
#ifdef RECORD_ABORT_ADDRESSES
    ~tm_debugCounters() {
        // output abort addresses
        FILE * pFile;
        pFile = fopen ("abortaddresses.txt","w");
        for (int i=0;i<numAbortAddr;++i) {
            fprintf(pFile, "%lx\n", abortAddr[i]);
        }
        fclose(pFile);
        cout<<"NUM_ABORT_ADDRESSES="<<numAbortAddr<<endl;
        cout<<"NUM_ABORT_ADDRESSES_EXACT="<<numAbortAddrExact<<endl;
    }
#endif
};

















#define TIMING_PROBABILITY_THRESH 0.01

template <int NUM_PROCESSES>
void countersProbStartTime(tm_debugCounters<NUM_PROCESSES>& counters, const int tid, const double randomZeroToOne) {
    if (randomZeroToOne < TIMING_PROBABILITY_THRESH) {
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
        __TM_COUNTER_SET(timingTemp, now);
    }
}

template <int NUM_PROCESSES>
void countersProbEndTime(tm_debugCounters<NUM_PROCESSES>& counters, const int tid /*, tm_counter_t * timingCounter*/) {
    long long start = __TM_COUNTER_GET(timingTemp);
    if (start > 0) {
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
        __TM_COUNTER_SET(timingTemp, 0);
        __TM_COUNTER_SET(timingOnFallback, now-start);
        //counterAdd(timingCounter, tid, (now-start)/*/TIMING_PROBABILITY_THRESH*/);
    }
}

template <int NUM_PROCESSES>
void registerHTMAbort(tm_debugCounters<NUM_PROCESSES>& counters, const int tid, const XBEGIN_ARG_T arg, const int path) {
#ifdef RECORD_ABORTS
    int s = 0;
    char userAbortName = 0;
    if (arg) {
#   if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
        // FOR P8 TEXASR BIT DEFINITIONS, SEE PAGE 803 IN (http://fileadmin.counters.lth.se/counters/education/EDAN25/PowerISA_V2.07_PUBLIC.pdf)
#       define P8_MASK_ABORT_CODE ((1<<7)-1)
#       define P8_BIT_PERSISTENT 7
#       define P8_BIT_DISALLOWED 8
#       define P8_BIT_NESTING 9
#       define P8_BIT_FOOTPRINT 10
#       define P8_BIT_CONFLICT_SELF 11
#       define P8_BIT_CONFLICT_NONTX 12
#       define P8_BIT_CONFLICT_TX 13
#       define P8_BIT_CONFLICT_TLBINVAL 14
#       define P8_BIT_IMPL_SPECIFIC 15
#       define P8_BIT_CONFLICT_INSTRFETCH 16
#       define P8_MASK_RESERVED ((((1LL<<31)-1)&(~((1<<17)-1))) | (1LL<<33) | (((1LL<<52)-1)&(~((1LL<<39)-1))))
#       define P8_BIT_EXPLICIT 31
#       define P8_BIT_SUSPENDED 32
#       define P8_MASK_PRIVILEGED ((1LL<<34)|(1LL<<35))
#       define P8_BIT_ABORTADDR_EXACT 37

#       define getbit(bit) ((texasr>>(bit))&1)
        unsigned long texasr = __builtin_get_texasr();
        unsigned long tfiah = __builtin_get_tfiar();

#       define BIT_USER 1
#       define BIT_CAPACITY 2
#       define BIT_CONFLICT 3
#       define BIT_PERSISTENT 4
#       define BIT_ILLEGAL 5
#       define BIT_OTHER 6
#       define BIT_RESERVED 7
#       define BIT_PRIVILEGED 8
#       define BIT_USER_NAME_START 9
#       define NUM_USER_NAME_BITS 3

        s = (getbit(P8_BIT_EXPLICIT) << BIT_USER)
          | (getbit(P8_BIT_FOOTPRINT) << BIT_CAPACITY)
          | ((getbit(P8_BIT_CONFLICT_SELF) | getbit(P8_BIT_CONFLICT_NONTX) | getbit(P8_BIT_CONFLICT_TX) | getbit(P8_BIT_CONFLICT_TLBINVAL) | getbit(P8_BIT_CONFLICT_INSTRFETCH)) << BIT_CONFLICT)
          | (getbit(P8_BIT_PERSISTENT) << BIT_PERSISTENT)
          | (getbit(P8_BIT_DISALLOWED) << BIT_ILLEGAL)
          | ((getbit(P8_BIT_NESTING) | getbit(P8_BIT_IMPL_SPECIFIC) | getbit(P8_BIT_SUSPENDED)) << BIT_OTHER)
          | (((texasr & P8_MASK_RESERVED) > 0) << BIT_RESERVED)
          | (((texasr & P8_MASK_PRIVILEGED) > 0) << BIT_PRIVILEGED);
        if (getbit(P8_BIT_EXPLICIT)) {
            s |= (((texasr&P8_MASK_ABORT_CODE) & /* further limit abort code size: */ ((1<<NUM_USER_NAME_BITS)-1)) << (BIT_USER_NAME_START));
        }
#       ifdef RECORD_ABORT_ADDRESSES
        if (tid == 0 && (texasr & P8_MASK_RESERVED)) {
            //long a = X_ABORT_FAILURE_ADDRESS(arg);
            if (/*a &&*/ numAbortAddr < MAX_ABORT_ADDR) {
                //abortAddr[numAbortAddr++] = a;
                abortAddr[numAbortAddr++] = tfiah;
                if (getbit(P8_BIT_ABORTADDR_EXACT)) ++numAbortAddrExact;
            }
        }
#       endif
#   else
#       define BIT_USER 1
#       define BIT_CAPACITY 2
#       define BIT_CONFLICT 3
#       define BIT_RETRY 4
#       define BIT_ILLEGAL 5
#       define BIT_USER_NAME_START 6
#       define NUM_USER_NAME_BITS 3

        s = (X_ABORT_STATUS_IS_USER(arg) << BIT_USER)
          | (X_ABORT_STATUS_IS_CAPACITY(arg) << BIT_CAPACITY)
          | (X_ABORT_STATUS_IS_CONFLICT(arg) << BIT_CONFLICT)
          | (X_ABORT_STATUS_IS_RETRY(arg) << BIT_RETRY)
          | (X_ABORT_STATUS_IS_ILLEGAL(arg) << BIT_ILLEGAL)
          | (X_ABORT_STATUS_IS_USER_NAMED(arg, &userAbortName) << BIT_USER_NAME_START);
        if (s & (1<<BIT_USER_NAME_START)) s |= ((userAbortName&((1<<NUM_USER_NAME_BITS)-1)) << BIT_USER_NAME_START);
#   endif
    }
    if (s >= MAX_ABORT_STATUS) {
        cout<<"ERROR: s ("<<s<<") >= MAX_ABORT_STATUS ("<<MAX_ABORT_STATUS<<")"<<endl;
        exit(-1);
    }
    __TM_COUNTER_ADD(htmAbort[path*MAX_ABORT_STATUS+s], 1);
#else
    __TM_COUNTER_ADD(htmAbort[path], 1);
#endif
}























string cpp_getAutomaticAbortNames(const int compressedStatus) {
    stringstream ss;
#ifdef RECORD_ABORTS
#if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
    if (compressedStatus & (1<<BIT_CAPACITY)) ss<<" capacity";
    if (compressedStatus & (1<<BIT_CONFLICT)) ss<<" conflict";
    if (compressedStatus & (1<<BIT_PERSISTENT)) ss<<" persistent";
    if (compressedStatus & (1<<BIT_ILLEGAL)) ss<<" illegal";
    if (compressedStatus & (1<<BIT_OTHER)) ss<<" other";
    if (compressedStatus & (1<<BIT_RESERVED)) ss<<" reserved";
    if (compressedStatus & (1<<BIT_PRIVILEGED)) ss<<" privileged";
#else
    if (compressedStatus & (1<<BIT_USER)) ss<<" explicit";
    if (compressedStatus & (1<<BIT_CAPACITY)) ss<<" capacity";
    if (compressedStatus & (1<<BIT_CONFLICT)) ss<<" conflict";
    if (compressedStatus & (1<<BIT_RETRY)) ss<<" retry";
#endif
#endif
    return ss.str();
//    return "";
}

string cpp_getExplicitAbortName(const int compressedStatus) {
    stringstream ss;
#ifdef RECORD_ABORTS
#if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
    int explicitCode = (compressedStatus>>BIT_USER_NAME_START);
    if (compressedStatus & (1<<BIT_USER)) {
        ss<<"explicit("<<explicitCode<<")";
    }
#else

#endif
#endif
    return ss.str();
}

string cpp_getAllAbortNames(const int compressedStatus) {
    stringstream ss;
    ss<<cpp_getExplicitAbortName(compressedStatus)<<cpp_getAutomaticAbortNames(compressedStatus);
    return ss.str();
//    return "";
}

template <int NUM_PROCESSES>
void countersPrint(tm_debugCounters<NUM_PROCESSES>& counters) {
    string PATH_NAMES[] = {"fast", "middle", "fallback"};
    for (int path=0;path<NUMBER_OF_PATHS;++path) {
        switch (path) {
            case PATH_FAST_HTM: cout<<"[" << PATH_NAMES[path] << "]" << endl; break;
            case PATH_FALLBACK: cout<<"[" << PATH_NAMES[path] << "]" << endl; break;
        }
        if (__TM_COUNTER_TOTAL(htmCommit[path], NUM_PROCESSES)) {
            cout<<"total "<<PATH_NAMES[path]<<" commit         : "<<__TM_COUNTER_TOTAL(htmCommit[path], NUM_PROCESSES)<<endl;
#ifdef RECORD_ABORTS
            long long totalAborts = 0;
            for (int i=0;i<MAX_ABORT_STATUS;++i) {
                int _total = __TM_COUNTER_TOTAL(htmAbort[path*MAX_ABORT_STATUS+i], NUM_PROCESSES);
                if (_total) {
                    cout<<"    "<<PATH_NAMES[path]<<" abort            : "<<(i==0?"unexplained":cpp_getAllAbortNames(i))<<" "<<_total<<endl;
                    totalAborts += _total;
                }
            }
            cout<<"total "<<PATH_NAMES[path]<<" abort          : "<<totalAborts<<endl;
#endif
        }
    }
    cout<<"seconds global lock is held   : "<<(__TM_COUNTER_TOTAL(timingOnFallback, NUM_PROCESSES)/1000000000.)<<endl;
//    cout<<"jump to slow                  : "<<(__TM_COUNTER_TOTAL(cs->jumpToSlow, NUM_PROCESSES))<<endl;
}





/**
 * static globals
 */

static tm_debugCounters<MAX_TID_POW2> __tm_counters;

#endif	/* THEADCOUNTERS_H */

