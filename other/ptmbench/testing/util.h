#pragma once

#ifndef MAX_THREADS
#define MAX_THREADS 256
#endif

#ifndef PADDING_BYTES
#define PADDING_BYTES 128
#endif

#ifndef DEBUG
#define DEBUG if(0)
#define DEBUG1 if(0)
#define DEBUG2 if(0)
#endif

#ifndef VERBOSE
#define VERBOSE if(0)
#endif

#ifndef TRACE
#define TRACE if(0)
#endif

#ifndef TPRINT
#define TPRINT(str) cout<<"tid="<<tid<<": "<<str;
#endif

#define PRINT(name) { cout<<(#name)<<"="<<name<<endl; }

#include <chrono>
#include <vector>
#include <sstream>
#include <string>
#include <algorithm>

class ElapsedTimer {
private:
    char padding0[PADDING_BYTES];
    bool calledStart = false;
    char padding1[PADDING_BYTES];
    std::chrono::time_point<std::chrono::high_resolution_clock> start;
    char padding2[PADDING_BYTES];
public:
    void startTimer() {
        calledStart = true;
        start = std::chrono::high_resolution_clock::now();
    }
    int64_t getElapsedMillis() {
        if (!calledStart) {
            printf("ERROR: called getElapsedMillis without calling startTimer\n");
            exit(1);
        }
        auto now = std::chrono::high_resolution_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(now - start).count();
    }
};

class PaddedRandom {
private:
    volatile char padding[PADDING_BYTES-sizeof(unsigned int)];
    unsigned int seed;
public:
    PaddedRandom(void) {
        this->seed = 0;
    }
    PaddedRandom(int seed) {
        this->seed = seed;
    }
    
    void setSeed(int seed) {
        this->seed = seed;
    }
    
    /** returns pseudorandom x satisfying 0 <= x < n. **/
    unsigned int nextNatural() {
        seed ^= seed << 6;
        seed ^= seed >> 21;
        seed ^= seed << 7;
        return seed;
    }
};

class debugCounter {
private:
    struct PaddedVLL {
        volatile char padding[PADDING_BYTES-sizeof(long long)];
        volatile long long v;
    };
    PaddedVLL data[MAX_THREADS+1];
public:
    void add(const int tid, const long long val) {
        data[tid].v += val;
    }
    void inc(const int tid) {
        add(tid, 1);
    }
    long long get(const int tid) {
        return data[tid].v;
    }
    long long getTotal() {
        long long result = 0;
        for (int tid=0;tid<MAX_THREADS;++tid) {
            result += get(tid);
        }
        return result;
    }
    void clear() {
        for (int tid=0;tid<MAX_THREADS;++tid) {
            data[tid].v = 0;
        }
    }
    debugCounter() {
        clear();
    }
} __attribute__((aligned(PADDING_BYTES)));

struct TryLock {
    int volatile state;
    TryLock() {
        state = 0;
    }
    bool tryAcquire() {
        int read = state;
        if (read & 1) return false;
        return __sync_bool_compare_and_swap(&state, read, read|1); // prevents compiler & processor reordering
    }
    void acquire() {
        while (!tryAcquire()) { /* wait */ }
    }
    void release() {
        __asm__ __volatile__ ("":::"memory"); // prevent COMPILER reordering (no dangerous processor reordering on x86/64)
        ++state;
    }
    bool isHeld() {
        return state & 1;
    }
    int numberOfTimesAcquired() {
        return state >> 1;
    }
};

template <typename T>
class Renamer { // rename n values to compact names in A..Z,AA..ZZ,...
    static const int MAX_PAGES;
    char padding0[PADDING_BYTES];
    std::vector<T> data;
    int count;
    char padding1[PADDING_BYTES];
    std::string toString(int64_t ix) {
        std::stringstream ss;
        if (ix == 0) return "A";
        while (ix > 0) {
            ss<<(char)((ix%26)+'A');
            ix /= 26;
        }
        std::string s = ss.str();
        std::reverse(s.begin(), s.end());
        return s;
    }
public:
    Renamer() {}
    std::string operator[](T x) {
        int64_t foundIx = -1;
        for (int i=0;i<data.size();++i) {
            if (data[i] == x) {
                foundIx = i;
                break;
            }
        }
        if (foundIx == -1) {
            data.push_back(x);
            foundIx = data.size() - 1;
        }
        return toString(foundIx);
    }
};
Renamer<int64_t> renamer;
#define ptr(var) ((uintptr_t) var)
#define addrinfo_header() int _____cix=0; printf("    %s%36s ⠇ %7s %8s %8s %12s %18s%s\n", "\e[48;2;132;157;184m\e[38;2;0;0;0m", "symbol", "page_cl", "cl_off", "page_off", "page_no", "addr", "\e[0m")
#define addrinfo_str(var,str) printf("    %s%36s ⠇ %4s-%-2ld %8ld %8ld %12ld %18ld%s\n", (((++_____cix)%2)?"\e[48;2;37;46;54m":"\e[48;2;19;25;31m"), str, renamer[ptr(var)/4096].c_str(), (ptr(var)%4096)/64, ptr(var)%64, ptr(var)%4096, ptr(var)/4096, ptr(var), "\e[0m")
#define addrinfo(var) addrinfo_str(var,#var)
#define addrinfo_array(var,sz) \
    for (int _____i=0;_____i<(sz);++_____i) { \
        stringstream ss; \
        ss<<#var; \
        ss<<"["; \
        ss<<_____i; \
        ss<<"]"; \
        addrinfo_str(var[_____i], ss.str().c_str()); \
    }

/**
 * about prefetchers and impact of disabling them on a fully thread saturated experiment:
 * see https://software.intel.com/en-us/articles/disclosure-of-hw-prefetcher-control-on-some-intel-processors
 
    for ((d=0;d<16;++d)) ; do echo "d=$d" ; sudo wrmsr -a 0x1a4 $d ; for ((t=0;t<5;++t)); do ./benchmark.out -t 5000 -s 1000000 -n 144 -k 16 | grep throughput; done ; sudo wrmsr -a 0x1a4 0 ; done

    d=0
    throughput           : 3179208
    throughput           : 328785
    throughput           : 271181
    throughput           : 271662
    throughput           : 339177
    d=1
    throughput           : 21284737
    throughput           : 21371612
    throughput           : 21586596
    throughput           : 21537580
    throughput           : 21453861
    d=2
    throughput           : 281828
    throughput           : 17761501
    throughput           : 20333602
    throughput           : 8884166
    throughput           : 300936
    d=3
    throughput           : 21782862
    throughput           : 21933943
    throughput           : 22117844
    throughput           : 21690838
    throughput           : 21856681
    d=4
    throughput           : 271559
    throughput           : 285500
    throughput           : 268165
    throughput           : 274382
    throughput           : 486218
    d=5
    throughput           : 21106395
    throughput           : 21263640
    throughput           : 21462408
    throughput           : 21091236
    throughput           : 21474816
    d=6
    throughput           : 279195
    throughput           : 274948
    throughput           : 473515
    throughput           : 20087095
    throughput           : 288854
    d=7
    throughput           : 21639694
    throughput           : 22072905
    throughput           : 22098566
    throughput           : 88692394
    throughput           : 22251193
    d=8
    throughput           : 137657659
    throughput           : 614088
    throughput           : 18299828
    throughput           : 136717729
    throughput           : 27054028
    d=9
    throughput           : 27095502
    throughput           : 27559517
    throughput           : 27492516
    throughput           : 27549030
    throughput           : 27635948
    d=10
    throughput           : 26572024
    throughput           : 26810998
    throughput           : 27101491
    throughput           : 26817967
    throughput           : 27104816
    d=11
    throughput           : 26859106
    throughput           : 27266630
    throughput           : 27445825
    throughput           : 27928333
    throughput           : 27775733
    d=12
    throughput           : 24789882
    throughput           : 24896493
    throughput           : 24603445
    throughput           : 235390
    throughput           : 24886434
    d=13
    throughput           : 24573156
    throughput           : 25037663
    throughput           : 25104785
    throughput           : 25059368
    throughput           : 24956361
    d=14
    throughput           : 24451199
    throughput           : 24356666
    throughput           : 24300898
    throughput           : 24754663
    throughput           : 24245946
    d=15
    throughput           : 24742423
    throughput           : 24694535
    throughput           : 24954138
    throughput           : 24471079
    throughput           : 24231848
 */