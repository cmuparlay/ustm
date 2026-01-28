/*
 * File:   debugcounters_impl.h
 * Author: trbot
 *
 * Created on June 21, 2016, 9:50 PM
 */

#ifndef C_DEBUGCOUNTERS_IMPL_H
#define	C_DEBUGCOUNTERS_IMPL_H

#ifdef	__cplusplus
extern "C" {
#endif

#include <stdio.h>
#include "debugcounters.h"

#ifdef RECORD_ABORT_ADDRESSES
#define MAX_ABORT_ADDR (1<<24)
volatile char padding0[PREFETCH_SIZE_BYTES];
int numAbortAddr = 0; // for thread 0
int numAbortAddrExact = 0; // for thread 0
long abortAddr[MAX_ABORT_ADDR];
volatile char padding1[PREFETCH_SIZE_BYTES];
#endif

void registerHTMAbort(struct c_debugCounters *cs, const int tid, const XBEGIN_ARG_T arg, const int path) {
#ifdef RECORD_ABORTS
    int s = 0;
    char userAbortName = 0;
    if (arg) {
#   if defined(__powerpc64__) || defined(__ppc64__) || defined(__PPC64__)
        if (MAX_ABORT_STATUS < 4096) { printf("ERROR: SET MAX_ABORT_STATUS to 4096 or GREATER to allow POWER8 abort tracking\n"); exit(-1); }

        // FOR P8 TEXASR BIT DEFINITIONS, SEE PAGE 803 IN (http://fileadmin.cs.lth.se/cs/education/EDAN25/PowerISA_V2.07_PUBLIC.pdf)
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
// #       define BIT_USER 1
// #       define BIT_CAPACITY 2
// #       define BIT_CONFLICT 3
// #       define BIT_RETRY 4
// #       define BIT_ILLEGAL 5
// #       define BIT_USER_NAME_START 6
// #       define NUM_USER_NAME_BITS 3

//         s = (X_ABORT_STATUS_IS_USER(arg) << BIT_USER)
//           | (X_ABORT_STATUS_IS_CAPACITY(arg) << BIT_CAPACITY)
//           | (X_ABORT_STATUS_IS_CONFLICT(arg) << BIT_CONFLICT)
//           | (X_ABORT_STATUS_IS_RETRY(arg) << BIT_RETRY)
//           | (X_ABORT_STATUS_IS_ILLEGAL(arg) << BIT_ILLEGAL)
//           | (X_ABORT_STATUS_IS_USER_NAMED(arg, &userAbortName) << BIT_USER_NAME_START);
//         if (s & (1<<BIT_USER_NAME_START)) s |= ((userAbortName&((1<<NUM_USER_NAME_BITS)-1)) << BIT_USER_NAME_START);

#       define BIT_USER 0
#       define BIT_CAPACITY 1
#       define BIT_CONFLICT 2

        s = (X_ABORT_STATUS_IS_USER(arg) << BIT_USER)
          | (X_ABORT_STATUS_IS_CAPACITY(arg) << BIT_CAPACITY)
          | (X_ABORT_STATUS_IS_CONFLICT(arg) << BIT_CONFLICT);

#   endif
    }
    if (s >= MAX_ABORT_STATUS) {
        cout<<"ERROR: s ("<<s<<") >= MAX_ABORT_STATUS ("<<MAX_ABORT_STATUS<<")"<<endl;
        exit(-1);
    }
    counterInc(cs->htmAbort[path*MAX_ABORT_STATUS+s], tid);
#endif
}

void countersClear(struct c_debugCounters *cs) {
    int j=0;
    for (;j<NUMBER_OF_PATHS;++j) {
#ifdef RECORD_ABORTS
        #pragma omp parallel for
        for (int i=0;i<MAX_ABORT_STATUS;++i) {
            counterClear(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
        }
#endif
        counterClear(cs->htmCommit[j]);
    }
//    counterClear(cs->jumpToSlow);
    counterClear(cs->garbage);
    counterClear(cs->timingTemp);
    counterClear(cs->timingOnFallback);
}

void countersInit(struct c_debugCounters *cs, const int numProcesses) {
    cs->NUM_PROCESSES = numProcesses;
    PRINT_ELAPSED_FROM_START();
#ifdef RECORD_ABORTS
    // #pragma omp parallel for schedule(static, 16) num_threads(64)
    for (int k=0;k<NUMBER_OF_PATHS*MAX_ABORT_STATUS;++k) {
        cs->htmAbort[k] = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
        counterInit(cs->htmAbort[k], cs->NUM_PROCESSES);
    }
#endif
    PRINT_ELAPSED_FROM_START();
    for (int j=0;j<NUMBER_OF_PATHS;++j) {
        cs->htmCommit[j] = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
        counterInit(cs->htmCommit[j], cs->NUM_PROCESSES);
    }
//    cs->jumpToSlow = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
//    counterInit(cs->jumpToSlow, cs->NUM_PROCESSES);
    cs->garbage = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->garbage, cs->NUM_PROCESSES);
    cs->timingTemp = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->timingTemp, cs->NUM_PROCESSES);
    cs->timingOnFallback = (struct c_debugCounter *) malloc(sizeof(struct c_debugCounter));
    counterInit(cs->timingOnFallback, cs->NUM_PROCESSES);
    PRINT_ELAPSED_FROM_START();
}

void countersDestroy(struct c_debugCounters *cs) {
    int j=0;
    for (;j<NUMBER_OF_PATHS;++j) {
#ifdef RECORD_ABORTS
        int i=0;
        for (;i<MAX_ABORT_STATUS;++i) {
            counterDestroy(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
            free(cs->htmAbort[j*MAX_ABORT_STATUS+i]);
        }
#endif
        counterDestroy(cs->htmCommit[j]);
        free(cs->htmCommit[j]);
    }
    counterDestroy(cs->garbage);
    free(cs->garbage);
//    counterDestroy(cs->jumpToSlow);
//    free(cs->jumpToSlow);
    counterDestroy(cs->timingTemp);
    free(cs->timingTemp);
    counterDestroy(cs->timingOnFallback);
    free(cs->timingOnFallback);

#ifdef RECORD_ABORT_ADDRESSES
    // output abort addresses
    FILE * pFile;
    pFile = fopen ("abortaddresses.txt","w");
    for (int i=0;i<numAbortAddr;++i) {
        fprintf(pFile, "%lx\n", abortAddr[i]);
    }
    fclose(pFile);
    cout<<"NUM_ABORT_ADDRESSES="<<numAbortAddr<<endl;
    cout<<"NUM_ABORT_ADDRESSES_EXACT="<<numAbortAddrExact<<endl;
#endif
}

#define TIMING_PROBABILITY_THRESH 0.01

void countersProbStartTime(struct c_debugCounters *cs, const int tid, const double randomZeroToOne) {
    if (randomZeroToOne < TIMING_PROBABILITY_THRESH) {
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
        counterSet(cs->timingTemp, tid, now);
    }
}
void countersProbEndTime(struct c_debugCounters *cs, const int tid, struct c_debugCounter *timingCounter) {
    long long start = counterGet(cs->timingTemp, tid);
    if (start > 0) {
        timespec t;
        clock_gettime(CLOCK_MONOTONIC, &t);
        long long now = t.tv_sec * 1000000000LL + t.tv_nsec;
        counterSet(cs->timingTemp, tid, 0);
        counterAdd(timingCounter, tid, (now-start)/*/TIMING_PROBABILITY_THRESH*/);
    }
}

#ifdef	__cplusplus
}
#endif

#endif	/* DEBUGCOUNTERS_IMPL_H */

