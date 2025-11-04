#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cassert>

#include "libitm.h"

#ifdef USTM
#include "../transactions.h"
#else
#include "../glue/other_glue.h"
#endif

//We use GCC_TM instead of GCCTM because we want to use libitm's ABI directly
#ifdef GCC_TM
using array_t = uint64_t;
#else
using array_t = stm::atomic<uint64_t>;
#endif

static constexpr int MAX_RATIO = 200;
//for threads to perform unnecessary instrumented reads
thread_local array_t private_data[MAX_RATIO];

static array_t* shared;

static volatile int stop_requested = 0;

struct thargs;

struct Args {
    int threads = 64;
    int iterations = 1000;
    size_t array_size = 1;
    std::string mode = "local";
    int unnecessary_read_ratio = 0;
    size_t (*get_idx)(struct thargs *a, int i); 
};

void print_usage(const char* prog) {
    fprintf(stderr,
        "Usage: %s [-t threads] [-i iterations] [-s array_size] [-m mode]\n"
        "  -t <num>    Number of threads (default: 1)\n"
        "  -i <num>    Iterations per thread (default: 1000)\n"
        "  -s <num>    Array size (default: 1)\n"
        "  -m <mode>   Mode of operation: local | always | random (default: local)\n"
        "  -r <num>    Number of unnecessary instrumented reads to perform for every write (default: 0)\n",
        prog);
}

Args parse_args(int argc, char** argv) {
    Args args;
    int opt;
    while ((opt = getopt(argc, argv, "t:i:s:m:hr:")) != -1) {
        switch (opt) {
        case 't':
            args.threads = atoi(optarg);
            break;
        case 'i':
            args.iterations = atoi(optarg);
            break;
        case 's':
            args.array_size = strtoull(optarg, nullptr, 10);
            break;
        case 'm':
            args.mode = optarg;
            if (args.mode != "local" && args.mode != "always" && args.mode != "random") {
                fprintf(stderr, "Invalid mode: %s\n", optarg);
                print_usage(argv[0]);
                exit(EXIT_FAILURE);
            }
            break;
        case 'r':
            args.unnecessary_read_ratio = atoi(optarg);
            break;
        case 'h':
        default:
            print_usage(argv[0]);
            exit(EXIT_FAILURE);
        }
    }

    // Basic sanity checks
    if (args.threads <= 0 || args.iterations <= 0 || args.array_size == 0) {
        fprintf(stderr, "Error: arguments must be positive.\n");
        print_usage(argv[0]);
        exit(EXIT_FAILURE);
    }

    return args;
}

struct thargs { int id; Args* gargs; };

//always returns the same id, maximum conflicts
size_t always_get_idx(struct thargs *a, int i) {
    return 0;
}

//each thread does ops on its own reference, minimum conflicts
size_t local_get_idx(struct thargs *a, int i) {
    return a->id;
}

//do ops randomly, conflicts depend on the size of the array
size_t random_get_idx(struct thargs *a, int i) {
    //wyhash random function
	uint64_t wyhash64_x = i + 0x60bee2bee120fc15;
	__uint128_t tmp = (__uint128_t) wyhash64_x * 0xa3b195354a39b70d;
	uint64_t m1 = (tmp >> 64) ^ tmp;
	tmp = (__uint128_t)m1 * 0x1b03738712fad5c9;
	uint64_t m2 = (tmp >> 64) ^ tmp;
	return (size_t) m2;
}


static inline uint64_t now_ns(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

void *thread_main(void *p) {
    struct thargs *a = (struct thargs*)p;
    int id = a->id;
    uint64_t local_count = 0;
    for (int i = 0; i < a->gargs->iterations && !stop_requested; i++) {
        size_t idx = a->gargs->get_idx(a, i) % a->gargs->array_size;

#ifdef GCC_TM //gcc's tm tends to over instrument, so we use their libitm directly
        uint32_t actions = _ITM_beginTransaction(pr_instrumentedCode);
        assert(actions & a_runInstrumentedCode && "ITM not running instrumented code correctly");
        for(int r = 0; r < a->gargs->unnecessary_read_ratio; r++) _ITM_RU8(&private_data[r]);
        uint64_t v = _ITM_RU8(&shared[idx]);
        v++;
        _ITM_WU8(&shared[idx], v);
        _ITM_commitTransaction();
#else
        stm::atomic_region([&] () {
            for(int r = 0; r < a->gargs->unnecessary_read_ratio; r++) private_data[r].load();
            uint64_t v = shared[idx].load();
            v++;
            shared[idx].store(v);
            return true;
        });
#endif
        local_count++;
    }
    return (void*)(uintptr_t)local_count;
}

int main(int argc, char **argv) {
    Args args = parse_args(argc, argv);

    printf("Threads: %d\n", args.threads);
    printf("Iterations: %d\n", args.iterations);
    printf("Array size: %zu\n", args.array_size);
    printf("Mode: %s\n", args.mode.c_str());
    printf("Unnecessary read ratio: %d\n", args.unnecessary_read_ratio);

    pthread_t th[args.threads];
    struct thargs thread_args[args.threads];

    shared = (array_t*) malloc(sizeof(array_t) * args.array_size);
    memset(shared, 0, sizeof(array_t) * args.array_size);

    if(args.mode == "local") args.get_idx = &local_get_idx;
    if(args.mode == "random") args.get_idx = &random_get_idx;
    if(args.mode == "always") args.get_idx = &always_get_idx;

    uint64_t t0 = now_ns();
    for (int i = 0; i < args.threads; i++) {
        thread_args[i].id = i;
        thread_args[i].gargs = &args;
        if (pthread_create(&th[i], NULL, thread_main, &thread_args[i]) != 0) {
            perror("pthread_create"); exit(1);
        }
    }

    uint64_t total_ops = 0;
    for (int i = 0; i < args.threads; i++) {
        void *res;
        pthread_join(th[i], &res);
        total_ops += (uint64_t)(uintptr_t)res;
    }
    uint64_t t1 = now_ns();
    double secs = (t1 - t0) / 1e9;


    printf("Threads: %d, iters/thread: %d\n", args.threads, args.iterations);
    printf("Total committed transactions: %lu\n", (unsigned long)total_ops);
    printf("Throughput: %.2f txns/sec\n", total_ops / secs);

    // Print some array samples to avoid dead-code elimination surprises
    for (int i = 0; i < args.array_size; i++) printf("shared[%d]=%lu ", i,
#ifdef GCC_TM
        (unsigned long)shared[i]);
#else
        (unsigned long)(shared[i].load()));
#endif
    printf("\n");

    free(shared);

    return 0;
}

