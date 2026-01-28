/*
 * File:   debugcounters_print.h
 * Author: trbot
 *
 * Created on June 21, 2016, 10:04 PM
 */

#ifndef C_DEBUGCOUNTERS_PRINT_H
#define	C_DEBUGCOUNTERS_PRINT_H

#include <string>
#include <sstream>
#include <iostream>
#include "../platform.h"
using namespace std;

#include "debugcounter_impl.h"
#include "debugcounters_impl.h"

#define TM_COUNTER_INC(name, tid) counterInc(__tm_counters->name, (tid))
#define TM_COUNTER_SET(name, val, tid) counterSet(__tm_counters->name, (tid), (val))
#define TM_COUNTER_ADD(name, val, tid) counterAdd(__tm_counters->name, (tid), (val))
#define TM_COUNTER_TOTAL(name, nthreads) counterGetTotal(__tm_counters->name)
#define TM_REGISTER_ABORT(path, xarg, tid) registerHTMAbort(__tm_counters, (tid), (xarg), (path))
#define TM_REGISTER_COMMIT(path, tid) TM_COUNTER_ADD(htmCommit[(path)], 1, (tid))
#define TM_TIMER_START(tid) countersProbStartTime(__tm_counters, (tid), 0.)
#define TM_TIMER_END(tid) countersProbEndTime(__tm_counters, (tid), __tm_counters->timingOnFallback)
#define TM_CLEAR_COUNTERS() countersClear(__tm_counters)
#define TM_PRINT_COUNTERS() countersPrint(__tm_counters)
#define TM_CREATE_COUNTERS() { __tm_counters = (c_debugCounters *) malloc(sizeof(c_debugCounters)); /*printf("sizeof(c_debugCounters)=%lu\n", sizeof(c_debugCounters));*/ countersInit(__tm_counters, MAX_TID_POW2); }
#define TM_DESTROY_COUNTERS() { free(__tm_counters); }

/**
 * static globals
 */
volatile char ___padding247[PREFETCH_SIZE_BYTES];
static c_debugCounters * __tm_counters;
volatile char ___padding371[PREFETCH_SIZE_BYTES];

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
    // if (compressedStatus & (1<<BIT_RETRY)) ss<<" retry";
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

void countersPrint(struct c_debugCounters *cs) {
    string PATH_NAMES[] = {"fast htm", "slow htm", "fallback"};
    for (int path=0;path<NUMBER_OF_PATHS;++path) {
        switch (path) {
            case PATH_FAST_HTM: cout<<"[" << PATH_NAMES[path] << "]" << endl; break;
            case PATH_SLOW_HTM: cout<<"[" << PATH_NAMES[path] << "]" << endl; break;
            case PATH_FALLBACK: cout<<"[" << PATH_NAMES[path] << "]" << endl; break;
        }
        if (counterGetTotal(cs->htmCommit[path])) {
            cout<<"total "<<PATH_NAMES[path]<<" commit         : "<<counterGetTotal(cs->htmCommit[path])<<endl;
#ifdef RECORD_ABORTS
            long long totalAborts = 0;
            for (int i=0;i<MAX_ABORT_STATUS;++i) {
                int _total = counterGetTotal(cs->htmAbort[path*MAX_ABORT_STATUS+i]);
                if (_total) {
                    cout<<"    "<<PATH_NAMES[path]<<" abort            : "<<(i==0?"unexplained":cpp_getAllAbortNames(i))<<" "<<_total<<endl;
                    totalAborts += _total;
                }
            }
            cout<<"total "<<PATH_NAMES[path]<<" abort          : "<<totalAborts<<endl;
#endif
        }
    }
//    cout<<"scaled time on fallback (nanos over all processes) : "<<(counterGetTotal(cs->timingOnFallback))<<endl;
    cout<<"seconds global lock is held   : "<<(counterGetTotal(cs->timingOnFallback)/1000000000.)<<endl;
//    cout<<"jump to slow                  : "<<(counterGetTotal(cs->jumpToSlow))<<endl;
}

#endif	/* DEBUGCOUNTERS_PRINT_H */

