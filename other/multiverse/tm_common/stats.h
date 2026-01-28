#pragma once

// #include "gstats.h"
#include "user_gstats_handler.h"

#define ENABLE_TM_STATS 1

thread_local bool enableStats = true;

#if defined(GSTATS_HANDLE_STATS) && defined(ENABLE_TM_STATS)	
	#define STATS_LIST_APPEND(tid, stat, num) 	if (enableStats) GSTATS_APPEND(tid, stat, num)
	#define STATS_COUNTER_ADD(tid, stat, num) 	if (enableStats) GSTATS_ADD(tid, stat, num)
	#define STATS_RESET_TIMER(tid, name) 		GSTATS_TIMER_RESET(tid, name)
	#define STATS_ELAPSED_TIMER(tid, name) 		GSTATS_TIMER_ELAPSED(tid, name)
#else
	#define STATS_LIST_APPEND(tid, stat, num)
	#define STATS_COUNTER_ADD(tid, stat, num)
#endif