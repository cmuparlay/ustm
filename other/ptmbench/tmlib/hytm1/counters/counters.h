/*
 * File:   desc_include.h
 * Author: trbot
 *
 * Created on February 4, 2017, 5:20 AM
 */

#ifndef DESC_INCLUDE_H
#define	DESC_INCLUDE_H

#ifndef DEBUG0
    #define DEBUG0 if(0)
#endif
#ifndef DEBUG1
    #define DEBUG1 DEBUG0 if(1)
#endif
#ifndef DEBUG2
    #define DEBUG2 DEBUG1 if(0)
#endif
#ifndef DEBUG3
    #define DEBUG3 DEBUG2 if(0)
#endif
#ifndef MAX_TID_POW2
    #if defined MAX_THREADS_POW2
        #define MAX_TID_POW2 MAX_THREADS_POW2
    #else
        #define MAX_TID_POW2 512
    #endif
#endif

#include <chrono>
#include <stdio.h>
extern std::chrono::time_point<std::chrono::high_resolution_clock> ____executionStartTimeDebug;
// #define PRINT_ELAPSED_FROM_START() { printf("%s:%d %dms\n", __FILE__, __LINE__, (int) std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - ____executionStartTimeDebug).count()); }
#define PRINT_ELAPSED_FROM_START()

/**
 * NOTE: THIS IS THE FILE TO INCLUDE, IF YOU WANT TO USE THE COUNTERS.
 */

#include "debugcounters_cpp.h"
//#include "threadcounters.h"

#endif	/* DESC_INCLUDE_H */

