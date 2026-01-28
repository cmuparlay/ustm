#pragma once

#include "../common/global_tm_defines.h"

#define EMPTY_KEY 0
#define EMPTY_KEY_TWO_COL {0, 0}
#define NO_VAL 0

#define TREE_DEGREE 16

enum RetCode : int {
    RETRY = 0,
    UNNECCESSARY = 0,
    FAILURE = -1,
    SUCCESS = 1,
};

#define PADDING_BYTES 128

#ifndef DISABLE_TM_ALLOCATOR
    #define ALLOC_NODE(size, sKey, w, l) TM_T::tmNew<Node>(size, sKey, w, l)
#else 
    #define ALLOC_NODE(size, sKey, w, l) new Node(size, sKey, w, l)
#endif

#if !defined(DISABLE_TM_ALLOCATOR)
    #define RECLAIM_NODE(node) TM_T::tmDelete<Node>(node);    
#else 
   #define RECLAIM_NODE(node)  
#endif

#define arraycopy(src, srcStart, dest, destStart, len)         \
    for (int ___i = 0; ___i < (len); ++___i) {                 \
        (dest)[(destStart) + ___i] = (src)[(srcStart) + ___i]; \
    }

#define tmarraycopy(src, srcStart, dest, destStart, len)         \
    for (int ___i = 0; ___i < (len); ++___i) {                 \
        (dest)[(destStart) + ___i] = ((src)[(srcStart) + ___i].load()); \
    }

template <typename K>
struct kvpair {
    K key;
    VAL_T val;
    kvpair() {}
};

template <typename K>
int kv_compare_single_col_key(const void *_a, const void *_b) {
    const kvpair<K> *a = (const kvpair<K> *)_a;
    const kvpair<K> *b = (const kvpair<K> *)_b;
    if (a->key < b->key) {
        return -1;
    }
    else if (a->key > b->key) {
        return 1;
    }
    else {
        return 0;
    }
}

inline int TwoColKeyCompare (const TwoColumnKey& k1, const TwoColumnKey& k2) {
    if (k1.col1 < k2.col1) {
        return -1;
    }
    if (k1.col1 > k2.col1) {
        return 1;
    }
    if (k1.col2 < k2.col2) {
        return -1;
    }
    if (k1.col2 > k2.col2) {
        return 1;
    }        
    return 0;
}

template<typename K>
int kv_compare_two_col_key(const void* _a, const void* _b) {        
    kvpair<K>* kv1 = (kvpair<K>*)(_a);
    kvpair<K>* kv2 = (kvpair<K>*)(_b);
    return TwoColKeyCompare(kv1->key, kv2->key);
}