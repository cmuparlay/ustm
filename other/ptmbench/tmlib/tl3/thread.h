#pragma once

class List;
//class TypeLogs;

class Thread {
public:
    PAD;
    long UniqID;
    volatile long Retries;
//    int* ROFlag; // not used by stamp
    int IsRO;
    int isFallback;
//    long Starts; // how many times the user called TxBegin
    long AbortsHW; // # of times hw txns aborted
    long AbortsSW; // # of times sw txns aborted
    long CommitsHW;
    long CommitsSW;
    unsigned long long rng;
    unsigned long long xorrng [1];
    // tmalloc_t* allocPtr;    /* CCM: speculatively allocated */
    // tmalloc_t* freePtr;     /* CCM: speculatively free'd */
//    TypeLogs* rdSet;
//    TypeLogs* wrSet;
//    TypeLogs* LocalUndo;
    List* rdSet;
    List* wrSet;
    sigjmp_buf* envPtr;
    PAD;

    Thread(long id);
    void destroy();
    void compileTimeAsserts();
};// __attribute__((aligned(CACHE_LINE_SIZE)));

