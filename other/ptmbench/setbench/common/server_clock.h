/*
 * File:   server_clock.h
 * Author: trbot
 *
 * Created on August 2, 2017, 6:37 PM
 */

#ifndef SERVER_CLOCK_H
#define SERVER_CLOCK_H

#if defined __x86_64__ && !defined CPU_FREQ_GHZ
#error "Must define CPU_FREQ_GHZ for server_clock.h on __x86_64__"
#endif

#include <time.h>
#include <sched.h>
#include <cstdio>

class DebugInitMessagerForServerClock {
public:
    DebugInitMessagerForServerClock() {
#if defined(__i386__) || defined(__x86_64__)
    printf("server_clock=rdtsc\n");
#else
    printf("server_clock=CLOCK_MONOTONIC\n");
#endif
    }
};
static DebugInitMessagerForServerClock __debugIMFSC;

inline uint64_t get_server_clock() {
#if defined(__i386__)
    // #warning using rdtsc in get_server_clock
    uint64_t ret;
    __asm__ __volatile__("rdtsc" : "=A" (ret));
#elif defined(__x86_64__)
    // #warning using rdtsc in get_server_clock
    unsigned hi, lo;
    __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
    uint64_t ret = ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
        ret = (uint64_t) ((double)ret / CPU_FREQ_GHZ);
#else
    // #warning using clock_monotonic in get_server_clock
    timespec * tp = new timespec;
    clock_gettime(CLOCK_MONOTONIC, tp);
    uint64_t ret = tp->tv_sec * 1000000000 + tp->tv_nsec;
#endif
    return ret;
}

timespec getUptimeTimespec() {
    // size_t ___uptime_nanos_at_startTime = sched_clock();
    // size_t ___start_time_seconds = (int) (sched_clock());
    //printf("REALTIME_START_PERF_FORMAT=%lu%s%lu\n", std::chrono::duration_cast<std::chrono::seconds>(g->startTime).count(), ".", 0)
    // auto time_point = std::chrono::system_clock::now();
    timespec uptime;
    clock_gettime(CLOCK_MONOTONIC, &uptime);
    return uptime;
}
#define printUptimeStampForPERF(label) { \
    SOFTWARE_BARRIER; \
    timespec ___currts = getUptimeTimespec(); \
    uint64_t ___currclock = get_server_clock(); \
    SOFTWARE_BARRIER; \
    printf("REALTIME_%s_PERF_FORMAT=%ld%s%ld\n", (label), ___currts.tv_sec, ".", ___currts.tv_nsec); \
    printf("REALTIME_%s_GET_SERVER_CLOCK=%lu\n", (label), (___currclock)); \
}

//class ClockSplitter {
//private:
//    uint64_t time;
//
//    inline uint64_t get_server_clock() {
//#if defined(__i386__)
//        uint64_t ret;
//        __asm__ __volatile__("rdtsc" : "=A" (ret));
//#elif defined(__x86_64__)
//        unsigned hi, lo;
//        __asm__ __volatile__ ("rdtsc" : "=a"(lo), "=d"(hi));
//        uint64_t ret = ( (uint64_t)lo)|( ((uint64_t)hi)<<32 );
//            ret = (uint64_t) ((double)ret / CPU_FREQ_GHZ);
//#else
//            timespec * tp = new timespec;
//        clock_gettime(CLOCK_REALTIME, tp);
//        uint64_t ret = tp->tv_sec * 1000000000 + tp->tv_nsec;
//#endif
//        return ret;
//    }
//
//public:
//    ClockSplitter() {}
//    void reset() {
//        time = get_server_clock();
//    }
//    uint64_t split() {
//        uint64_t old = time;
//        time = get_server_clock();
//        return time - old;
//    }
//};

#ifdef MEASURE_TIMELINE_STATS
    PAD;
    volatile bool ___timeline_use = 0;
    PAD;
    #ifndef ___MIN_INTERVAL_DURATION
    #   define ___MIN_INTERVAL_DURATION 0.1
    #endif
    #define TIMELINE_BLIP(tid, name) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            printf("timeline_blip_%s tid=%d start=%lu\n", (name), (tid), ___blipTime); \
        } \
    }
    #define TIMELINE_BLIP_Llu(tid, name, label_lu) { \
        if (___timeline_use) { \
            uint64_t ___blipTime = get_server_clock(); \
            printf("timeline_blip_%s tid=%d start=%lu label=%lu\n", (name), (tid), ___blipTime, (unsigned long) (label_lu)); \
        } \
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
    #define TIMELINE_START_C(tid, condition) \
        uint64_t ___startTime; \
        if (___timeline_use && (condition)) { \
            ___startTime = get_server_clock(); \
        }
    #define TIMELINE_START(tid) TIMELINE_START_C((tid), true)
    #define TIMELINE_END_C(tid, name, condition) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s tid=%d start=%lu end=%lu\n", (name), (tid), ___startTime, ___endTime); \
            } \
        } \
    }
    #define TIMELINE_END_C_Llu(tid, name, condition, label_lu) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                printf("timeline_%s tid=%d start=%lu end=%lu label=%lu\n", (name), (tid), ___startTime, ___endTime, (unsigned long) (label_lu)); \
            } \
        } \
    }
    #define TIMELINE_END_C_INMEM(tid, stat_name, condition) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                GSTATS_APPEND((tid), (stat_name), (___startTime)); \
                GSTATS_APPEND((tid), (stat_name), (___endTime)); \
                /*GSTATS_APPEND((tid), (stat_name), ((___endTime) - (___startTime)));*/ \
            } \
        } \
    }
    #define TIMELINE_END_C_INMEM_Llu(tid, stat_name, condition, label_lu) { \
        if (___timeline_use && (condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            auto ___duration_ms = (___endTime - ___startTime) / 1000000; \
            if (___duration_ms >= (___MIN_INTERVAL_DURATION)) { \
                GSTATS_APPEND((tid), (stat_name), (___startTime)); \
                GSTATS_APPEND((tid), (stat_name), (___endTime)); \
                GSTATS_APPEND((tid), (stat_name), ((unsigned long) (label_lu))); \
            } \
        } \
    }
    #define TIMELINE_END(tid, name) TIMELINE_END_C((tid), (name), true)
    #define TIMELINE_END_Llu(tid, name, label_lu) TIMELINE_END_C_Llu((tid), (name), true, (label_lu))
    #define TIMELINE_END_INMEM(tid, stat_name) TIMELINE_END_C_INMEM((tid), (stat_name), true)
    #define TIMELINE_END_INMEM_Llu(tid, stat_name, label_lu) TIMELINE_END_C_INMEM_Llu((tid), (stat_name), true, (label_lu))
#else
    #define TIMELINE_BLIP(tid, name)
    #define TIMELINE_BLIP_Llu(tid, name, label_lu)
    #define TIMELINE_BLIP_INMEM(tid, stat_name)
    #define TIMELINE_BLIP_INMEM_Llu(tid, stat_name, label_lu)
    #define TIMELINE_START(tid)
    #define TIMELINE_START_C(tid, condition)
    #define TIMELINE_END_C(tid, name, condition)
    #define TIMELINE_END_C_Llu(tid, name, condition, label_lu)
    #define TIMELINE_END_C_INMEM(tid, stat_name)
    #define TIMELINE_END_C_INMEM_Llu(tid, stat_name, label_lu)
    #define TIMELINE_END(tid, name)
    #define TIMELINE_END_Llu(tid, name, label_lu)
    #define TIMELINE_END_INMEM(tid, stat_name)
    #define TIMELINE_END_INMEM_Llu(tid, stat_name, label_lu)
#endif

#ifdef MEASURE_DURATION_STATS
    #define DURATION_START_C(tid, condition) \
        uint64_t ___startTime; \
        if ((condition)) { \
            ___startTime = get_server_clock(); \
        }
    #define DURATION_START(tid) DURATION_START_C((tid), true)
    #define DURATION_END_C(tid, stat_id, condition) { \
        if ((condition)) { \
            uint64_t ___endTime = get_server_clock(); \
            GSTATS_ADD((tid), (stat_id), (___endTime - ___startTime)); \
        } \
    }
    #define DURATION_END(tid, stat_id) DURATION_END_C((tid), (stat_id), true)
#else
    #define DURATION_START_C(tid, condition)
    #define DURATION_START(tid)
    #define DURATION_END_C(tid, stat_id, condition)
    #define DURATION_END(tid, stat_id)
#endif

#endif /* SERVER_CLOCK_H */
