/**
 * Preliminary C++ implementation of binary search tree using LLX/SCX.
 *
 * Copyright (C) 2014 Trevor Brown
 *
 */

#ifndef RANDOM_FNV1A_H
#define	RANDOM_FNV1A_H

#include "plaf.h"

// note: this is a hash function being used as an RNG and you shouldn't use this. for example, it alternates between even and odd numbers. and every 2^k-th number alternates the k-th bit.
class Random64 {
private:
    union {
        PAD;
        uint64_t seed;
    };
public:
    Random64() {
        this->seed = 0;
    }
    Random64(uint64_t seed) {
        this->seed = seed;
    }

    void setSeed(uint64_t seed) {
        this->seed = seed;
    }

    uint64_t next(uint64_t n) {
        uint64_t offset = 14695981039346656037ULL;
        uint64_t prime = 1099511628211;
        uint64_t hash = offset;
        hash ^= seed;
        hash *= prime;
        seed = hash;
        return hash % n;
    }

    size_t next() {
        uint64_t offset = 14695981039346656037ULL;
        uint64_t prime = 1099511628211;
        uint64_t hash = offset;
        hash ^= seed;
        hash *= prime;
        seed = hash;
        return hash;
    }

    double nextDouble() {
        return (next() / (double) std::numeric_limits<uint64_t>::max());
    }

};

#endif	/* RANDOM_H */

