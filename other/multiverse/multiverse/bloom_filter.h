#pragma once

#include <cstdint>
#include <atomic>

namespace ns_multiverse {
    class BloomFilter64 {
        #define BLOOM_FILTER_SIZE 64
    public:
        std::atomic<uint64_t> filter; // bloom filter data

        inline void init() {	
            filter.store(0, std::memory_order_release);
        }

        inline uint64_t hash(uint64_t* key, uint64_t seed) {
            uint64_t p = (uint64_t) key + seed;
    #ifdef __LP64__
            p ^= p >> 33;
            p *= 0xff51afd7ed558ccdULL;
            p ^= p >> 33;
            p *= 0xc4ceb9fe1a85ec53ULL;
            p ^= p >> 33;
    #else
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            return p;
        }

        inline bool contains(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));        
            // uint64_t bloom = filter.load(std::memory_order_acquire);
            uint64_t bloom = filter.load(std::memory_order_relaxed);
            // return (bloom & (bit1 | bit2));
            return (bloom & (bit1));
        }

        inline void insertFreshSafe(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));        
            // uint64_t bits = bit1 | bit2;
            uint64_t bits = bit1;
            filter.fetch_or(bits, std::memory_order_relaxed);
        }

        inline void insertFreshUnsafe(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));        
            // uint64_t bits = bit1 | bit2;
            uint64_t bloom = filter.load(std::memory_order_relaxed);
            uint64_t bits = bit1 | bloom;
            filter.store(bits, std::memory_order_relaxed);
        }
	};

    class BloomFilter64Volatile {
        #define BLOOM_FILTER_SIZE 64
    public:
        volatile uint64_t filter; // bloom filter data

        inline void init() {	
            filter = 0;
        }

        inline uint64_t hash(uint64_t* key, uint64_t seed) {
            uint64_t p = (uint64_t) key + seed;
    #ifdef __LP64__
            p ^= p >> 33;
            p *= 0xff51afd7ed558ccdULL;
            p ^= p >> 33;
            p *= 0xc4ceb9fe1a85ec53ULL;
            p ^= p >> 33;
    #else
            p ^= p >> 16;
            p *= 0x85ebca6b;
            p ^= p >> 13;
            p *= 0xc2b2ae35;
            p ^= p >> 16;
    #endif
            return p;
        }

        inline bool contains(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));                    
            // return (filter & (bit1 | bit2));
            return (filter & (bit1));
        }

        inline void insertFreshSafe(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));        
            // filter = filter | (bit1 | bit2);            
            __sync_fetch_and_or(&filter, bit1);
        }

        inline void insertFreshUnsafe(uint64_t* key) {
            uint64_t bit1 = (1 << (hash(key, 0) & (BLOOM_FILTER_SIZE-1)));
            // uint64_t bit2 = (1 << (hash(key, 2) & (BLOOM_FILTER_SIZE-1)));        
            // filter = filter | (bit1 | bit2);            
            filter = filter | bit1;
        }
	};
}