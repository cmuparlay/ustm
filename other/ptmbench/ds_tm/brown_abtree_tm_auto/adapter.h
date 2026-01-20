/**
 * @TODO: ADD COMMENT HEADER
 */

#pragma once

#include "errors.h"
#include "record_manager.h"
#include <csignal>
#include <iostream>
#ifdef USE_TREE_STATS
#include "tree_stats.h"
#endif
#include "abtree_tm.h"

// #define DEGREE 11
#define DEGREE 16
// #define DEGREE 3

#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, Node<K,V,DEGREE>>
#define DATA_STRUCTURE_T ABTreeTM<RECORD_MANAGER_T, K, V, DEGREE, std::less<K>>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
  private:
    const void *NO_VALUE;
    DATA_STRUCTURE_T * ds;

  public:
    ds_adapter(const int NUM_THREADS,
               const K &KEY_ANY,
               const K &KEY_MAX,
               const V &unused2,
               Random64 *const unused3)
        : ds(new DATA_STRUCTURE_T(NUM_THREADS, KEY_ANY, KEY_MAX)) {}

 ds_adapter(const int NUM_THREADS,
				const K &KEY_ANY,
                const K &KEY_MAX,
				const V &VALUE_RESERVED,
				Random64 * const unusedRngs,
				K const * prefilledKeysArray,
				V const * prefilledValsArray,
				int64_t expectedSize,
				int rand)
		: NO_VALUE(VALUE_RESERVED)
	{				
        ds = new DATA_STRUCTURE_T(NUM_THREADS, KEY_ANY, KEY_MAX);
        printf("DS alloc done starting prefill.\n");

        uint64_t size = 0;
        // for (uint64_t ie = 0; ie < 20; ie++) {
        //     ds->tryInsert(0, prefilledKeysArray[size], prefilledValsArray[size]);
        //     GSTATS_ADD(0, prefill_size, 1);            
        //     size++;                                
        // }
        for (uint64_t ie = 0; ie < expectedSize; ie++) {
            ds->tryInsert(0, prefilledKeysArray[size], prefilledValsArray[size]);
            GSTATS_ADD(0, prefill_size, 1);            
            size++;                                
        }

        // ds->validate();
        printf("DS prefill done.\n");
    }


    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return ds->NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread(tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    V insert(const int tid, const K &key, const V &val) {
        setbench_error("insert-replace functionality not implemented for this data structure");
    }

    V insertIfAbsent(const int tid, const K &key, const V &val) {
        return ds->tryInsert(tid, key, val);
    }

    V erase(const int tid, const K &key) {
        return ds->tryErase(tid, key);
    }

    V find(const int tid, const K &key) {
        return ds->find(tid, key);
    }

    bool contains(const int tid, const K &key) {
        return ds->contains(tid, key);
    }

    int rangeQuery(const int tid, const K &lo, const K &hi, K *const resultKeys, void **const resultValues) {
        setbench_error("unimplemented");
        return -1; //TODO
    }
    void printSummary() {
        ds->printDebuggingDetails();
    }
    bool validateStructure() {
        // printf("Validation disabled returning true always.\n");
        // return true;
        return ds->validate();
    }

    void printObjectSizes() {
        std::cout << "sizes: node="
                  << (sizeof(Node<K, V, DEGREE>))
                  << std::endl;
    }

#ifdef USE_TREE_STATS
    class NodeHandler {
      public:
        typedef nodeptr_unsafe NodePtrType;

        K minKey;
        K maxKey;

        NodeHandler(const K &_minKey, const K &_maxKey) {
            minKey = _minKey;
            maxKey = _maxKey;
        }

        class ChildIterator {
          private:
            size_t ix;
            NodePtrType node; // node being iterated over
          public:
            ChildIterator(NodePtrType _node) {
                node = _node;
                ix = 0;
            }
            bool hasNext() { return ix < node->size; }
            NodePtrType next() { return node->ptrs[ix++]; }
        };

        static bool isLeaf(NodePtrType node) { return node->leaf; }
        static ChildIterator getChildIterator(NodePtrType node) { return ChildIterator(node); }
        static size_t getNumChildren(NodePtrType node) { return node->size; }
        static size_t getNumKeys(NodePtrType node) { return node->leaf ? node->size : 0; }
        static size_t getSumOfKeys(NodePtrType node) {
            size_t sz = getNumKeys(node);
            size_t result = 0;
            for (size_t i = 0; i < DEGREE; ++i) {
                if (node->leaf) result += (size_t) node->keys[i];
            }
            return result;
        }
        static size_t getSizeInBytes(NodePtrType node) { return sizeof(*node); }
    };
    TreeStats<NodeHandler> *createTreeStats(const K &_minKey, const K &_maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), ds->getRoot(), true);
    }
#endif
};

