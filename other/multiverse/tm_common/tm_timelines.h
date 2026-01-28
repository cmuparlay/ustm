#pragma once

#include <cstdint>
#include "server_clock.h"

#ifdef MEASURE_TIMELINE_STATS    
	
	thread_local uint64_t timeline_starttime;	
	thread_local bool timeline_started = false;
	
	#ifndef ___MIN_INTERVAL_DURATION
    #   define ___MIN_INTERVAL_DURATION 0.1
    #endif

    #define TM_TIMELINE_BLIP(tid, name) { \
		uint64_t ___blipTime = get_server_clock(); \
		printf("blip_%s %d %lu\n", (name), (tid), ___blipTime); \
    }
    #define TM_TIMELINE_BLIP_Llu(tid, name, label_lu) { \
		uint64_t ___blipTime = get_server_clock(); \
		printf("blip_%s %d %lu %lu\n", (name), (tid), ___blipTime, (unsigned long) (label_lu)); \
    }
    #define TM_TIMELINE_BLIP_Ls(tid, name, label_s) { \
		uint64_t ___blipTime = get_server_clock(); \
		printf("blip_%s %d %lu %s\n", (name), (tid), ___blipTime, (unsigned long) (label_s)); \
    }
    #define TIMELINE_BLIP_INMEM(tid, stat_name) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            GSTATS_APPEND((tid), (stat_name), (___blipTime)); \
        } \
    }
    #define TIMELINE_BLIP_INMEM_Llu(tid, stat_name, label_lu) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            GSTATS_APPEND((tid), (stat_name), (___blipTime)); \
            GSTATS_APPEND((tid), (stat_name), ((unsigned long) (label_lu))); \
        } \
    }
    #define TM_TIMELINE_START_C(tid, condition) \
        if ((!timeline_started) && (condition)) { \
            timeline_started = true; \
			timeline_starttime = get_server_clock(); \
        }
    #define TM_TIMELINE_START(tid) TM_TIMELINE_START_C((tid), true)
    #define TM_TIMELINE_END_C(tid, name, condition) { \
        if ((timeline_started) && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - timeline_starttime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s %d %lu %lu\n", (name), (tid), timeline_starttime, ___endTime); \
            } \
			timeline_started = false; \
        } \
    }
    #define TM_TIMELINE_END_C_Llu(tid, name, condition, label_lu) { \
        if ((timeline_started) && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - timeline_starttime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s %d %lu %lu %lu\n", (name), (tid), timeline_starttime, ___endTime, (unsigned long) (label_lu)); \
            } \
			timeline_started = false; \
        } \
    }
    #define TM_TIMELINE_END_C_Ls(tid, name, condition, label_s) { \
        if ((timeline_started) && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - timeline_starttime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s %d %lu %lu %s\n", (name), (tid), timeline_starttime, ___endTime, (label_s)); \
            } \
			timeline_started = false; \
        } \
    }
    #define TM_TIMELINE_END_C_INMEM(tid, stat_name, condition) { \
        if (timeline_started && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - timeline_starttime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                GSTATS_APPEND((tid), (stat_name), (timeline_starttime)); \
                GSTATS_APPEND((tid), (stat_name), (___endTime)); \
                /*GSTATS_APPEND((tid), (stat_name), ((___endTime) - (timeline_starttime)));*/ \
            } \
            timeline_started = false; \
        } \
    }
    #define TM_TIMELINE_END_C_INMEM_Llu(tid, stat_name, condition, label_lu) { \
        if (timeline_started && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - timeline_starttime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                GSTATS_APPEND((tid), (stat_name), (timeline_starttime)); \
                GSTATS_APPEND((tid), (stat_name), (___endTime)); \
                GSTATS_APPEND((tid), (stat_name), ((unsigned long) (label_lu))); \
            } \
            timeline_started = false; \
        } \
    }
    #define TM_TIMELINE_END(tid, name) TM_TIMELINE_END_C((tid), (name), true)
    #define TM_TIMELINE_END_Llu(tid, name, label_lu) TM_TIMELINE_END_C_Llu((tid), (name), true, (label_lu))
    #define TM_TIMELINE_END_INMEM(tid, stat_name) TM_TIMELINE_END_C_INMEM((tid), (stat_name), true)
    #define TM_TIMELINE_END_INMEM_Llu(tid, stat_name, label_lu) TM_TIMELINE_END_C_INMEM_Llu((tid), (stat_name), true, (label_lu))
#else
    #define TM_TIMELINE_BLIP(tid, name)
    #define TM_TIMELINE_BLIP_Llu(tid, name, label_lu)
    #define TM_TIMELINE_BLIP_Ls(tid, name, label_s)
    #define TM_TIMELINE_START(tid)
    #define TM_TIMELINE_START_C(tid, condition)

    #define TM_TIMELINE_END_C(tid, name, condition)
    #define TM_TIMELINE_END_C_Llu(tid, name, condition, label_lu)
    #define TM_TIMELINE_END_C_Ls(tid, name, condition, label_s)
    #define TM_TIMELINE_END(tid, name)
    #define TM_TIMELINE_END_Llu(tid, name, label_lu)
    #define TM_TIMELINE_END_Ls(tid, name, label_s)
    #define TM_TIMELINE_END_INMEM(tid, stat_name) 
    #define TM_TIMELINE_END_INMEM_Llu(tid, stat_name, label_lu) 
    #define TM_TIMELINE_END_C_INMEM(tid, stat_name, condition)
    #define TM_TIMELINE_END_C_INMEM_Llu(tid, stat_name, condition, label_lu)

    #define TM_TIMELINE_BLIP_INMEM(tid, stat_name)
    #define TM_TIMELINE_BLIP_INMEM_Llu(tid, stat_name, label_lu)
#endif