#pragma once

class vLock;

/* list element (serves as an entry in a read/write set) */
class AVPair {
public:
    AVPair* Next;
    AVPair* Prev;
    volatile intptr_t* addr;
    intptr_t value;
//    union {
//        long l;
//#ifdef __LP64__
//        float f[2];
//#else
//        float f[1];
//#endif
//        intptr_t p;
//    } value;
    vLock* LockFor;     /* points to the vLock covering addr */
    uint64_t rdv;  /* read-version @ time of 1st read - observed */
    long Ordinal;
    AVPair** hashTableEntry;
//    int32_t hashTableIx;

    AVPair();
    AVPair(AVPair* _Next, AVPair* _Prev, long _Ordinal);
    void validateInvariants();
} __attribute__((aligned(64)));
