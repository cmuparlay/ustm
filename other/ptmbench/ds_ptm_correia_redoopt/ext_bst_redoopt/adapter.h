
/**
 * Trevor Brown, 2020.
 */

#pragma once
#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif
#include "ext_bst_tm.h"

#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, Node<K, V>>
#define DATA_STRUCTURE_T ext_bst_tm<K, V, std::less<K>, RECORD_MANAGER_T>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * ds;

public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_RESERVED,
               const K& unused1,
               const V& VALUE_RESERVED,
               Random64 * const unused2)
    : NO_VALUE(VALUE_RESERVED)
    {                
        ds = new DATA_STRUCTURE_T(KEY_RESERVED, NO_VALUE, NUM_THREADS);
    }	
 
	ds_adapter(const int NUM_THREADS,
				const K &KEY_MIN,
				const K &KEY_MAX,
				const V &VALUE_RESERVED,
				Random64 * const unusedRngs,
				K const * prefilledKeysArray,
				V const * prefilledValsArray,
				int64_t expectedSize,
				int rand)
		: NO_VALUE(VALUE_RESERVED)
	{				
        ds = new DATA_STRUCTURE_T(KEY_MIN, NO_VALUE, NUM_THREADS);
        uint64_t size = 0;
        for (uint64_t ie = 0; ie < expectedSize/100; ie++) {
            TM::template updateTx<bool>([&] () {
                for (uint64_t k = 0; k < 100; k++) {
                    ds->insertIfAbsent(0, prefilledKeysArray[size+k], prefilledValsArray[size+k]);
                    GSTATS_ADD(0, prefill_size, 1);
                }
                return true;
            });
        }
        if (expectedSize%100 != 0) {
            TM::template updateTx<bool>([&] () {
                for (uint64_t k = 0; k < expectedSize%100; k++) {
                    ds->insertIfAbsent(0, prefilledKeysArray[size+k], prefilledValsArray[size+k]);
                    GSTATS_ADD(0, prefill_size, 1);
                }
                return true;
            });
        }

    }

    ~ds_adapter() {
        delete ds;
    }

    V getNoValue() {
        return NO_VALUE;
    }

    void initThread(const int tid) {
        ds->initThread(tid);
    }
    void deinitThread(const int tid) {
        ds->deinitThread(tid);
    }

    bool contains(const int tid, const K& key) {
        return ds->contains(tid, key);
    }
    V insert(const int tid, const K& key, const V& val) {
        // return ds->insert(tid, key, val);
        setbench_error("No implemented\n");
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        return ds->insertIfAbsent(tid, key, val);
    }
    V erase(const int tid, const K& key) {
        return ds->erase(tid, key);
    }
    V find(const int tid, const K& key) {
        return ds->find(tid, key);
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        // return ds->rangeUpdate(tid, lo, hi);        
        setbench_error("No implemented\n");
    }
    void printSummary() {
        // auto recmgr = ds->debugGetRecMgr();
        // recmgr->printStatus();
    }
    bool validateStructure() {
        return true;
    }
    void printObjectSizes() {
        std::cout<<"sizes: node="
                 <<(sizeof(nodeptr_unsafe))
                 <<std::endl;
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {
        ds->debugGetRecMgr()->debugGCSingleThreaded();
    }

#ifdef USE_TREE_STATS
    class NodeHandler {
    public:
        typedef nodeptr_unsafe NodePtrType;
        K minKey;
        K maxKey;

        NodeHandler(const K& _minKey, const K& _maxKey) {
            minKey = _minKey;
            maxKey = _maxKey;
        }

        class ChildIterator {
        private:
            bool leftDone;
            bool rightDone;
            NodePtrType node; // node being iterated over
        public:
            ChildIterator(NodePtrType _node) {
                node = _node;
                if (!node) {
                    leftDone = true;
                    rightDone = true;
                } else {
                    leftDone = node->left == NULL;
                    rightDone = node->right == NULL;
                }
            }
            bool hasNext() {
                return !(leftDone && rightDone);
            }
            NodePtrType next() {
                if (!leftDone) {
                    leftDone = true;
                    return node->left;
                }
                if (!rightDone) {
                    rightDone = true;
                    return node->right;
                }
                setbench_error("ERROR: it is suspected that you are calling ChildIterator::next() without first verifying that it hasNext()");
            }
        };

        static bool isLeaf(NodePtrType node) {
            if (!node) return true;
            return (node->left == NULL) && (node->right == NULL);
        }
        static size_t getNumChildren(NodePtrType node) {
            if (!node) return 0;
            return (node->left != NULL) + (node->right != NULL);
        }
        static size_t getNumKeys(NodePtrType node) {
            if (!node) return 0;
            if (!isLeaf(node)) return 0;
            //if (node->key == KEY_RESERVED) return 0;
            return 1;
        }
        static size_t getSumOfKeys(NodePtrType node) {
            if (!node) return 0;
            if (!isLeaf(node)) return 0;
            //if (node->key == KEY_RESERVED) return 0;
            return (size_t) node->key;
        }
        static ChildIterator getChildIterator(NodePtrType node) {
            return ChildIterator(node);
        }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), ds->debug_getEntryPoint()->left->left, true);
    }
#endif
};
