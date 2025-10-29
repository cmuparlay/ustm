/**
 * Copyright Trevor Brown, 2020.
 */

#ifndef BST_ADAPTER_H
#define BST_ADAPTER_H

#include "errors.h"
#ifdef USE_TREE_STATS
#   include "tree_stats.h"
#endif
#include "impl.h"

#ifdef ORACLE_VALIDATION
    #include <set>
    using namespace std;
    set<test_type> oracle;
#endif

#define RECORD_MANAGER_T record_manager<Reclaim, Alloc, Pool, Node<K, V>>
#define DATA_STRUCTURE_T brown_int_bst_tm<K, V, std::less<K>, RECORD_MANAGER_T>

template <typename K, typename V, class Reclaim = reclaimer_debra<K>, class Alloc = allocator_new<K>, class Pool = pool_none<K>>
class ds_adapter {
private:
    const V NO_VALUE;
    DATA_STRUCTURE_T * const ds;

public:
    ds_adapter(const int NUM_THREADS,
               const K& KEY_RESERVED,
               const K& unused1,
               const V& VALUE_RESERVED,
               Random64 * const unused2)
    : NO_VALUE(VALUE_RESERVED)
    , ds(new DATA_STRUCTURE_T(KEY_RESERVED, NO_VALUE, NUM_THREADS))
    {}
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

#ifdef ORACLE_VALIDATION
    void fullOracleCompare() {
        for (K k = 1; k <= MAXKEY; ++k) {
            if ((oracle.find(k) == oracle.end()) != (ds->find(tid, k) == getNoValue())) {
                if (oracle.find(k) == oracle.end()) printf("key %lld not in oracle, but in ds\n", k);
                else printf("key %lld in oracle, but not in ds\n", k);
                assert(false);
            }
        }
    }
#endif

    bool contains(const int tid, const K& key) {
        bool retval = ds->find(tid, key) != getNoValue();
#ifdef ORACLE_VALIDATION
        assert(retval == (oracle.find(key) != oracle.end()));
#endif
        return retval;
    }
    V insert(const int tid, const K& key, const V& val) {
        V retval = ds->insert(tid, key, val);
#ifdef ORACLE_VALIDATION
        // assert((retval == getNoValue()) == (oracle.find(key) == oracle.end()));
        oracle.insert(key);
        fullOracleCompare();
#endif
        return retval;
    }
    V insertIfAbsent(const int tid, const K& key, const V& val) {
        V retval = ds->insertIfAbsent(tid, key, val);
#ifdef ORACLE_VALIDATION
        // assert((retval == getNoValue()) == (oracle.find(key) == oracle.end()));
        oracle.insert(key);
        fullOracleCompare();
#endif
        return retval;
    }
    V erase(const int tid, const K& key) {
        V retval = ds->erase(tid, key);
#ifdef ORACLE_VALIDATION
        // assert((retval == getNoValue()) == (oracle.find(key) == oracle.end()));
        oracle.erase(key);
        fullOracleCompare();
#endif
        return retval;
    }
    V find(const int tid, const K& key) {
        V retval = ds->find(tid, key);
#ifdef ORACLE_VALIDATION
        assert((retval == getNoValue()) == (oracle.find(key) == oracle.end()));
#endif
        return retval;
    }
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        setbench_error("not implemented");
    }
    void printSummary() {
        auto recmgr = ds->debugGetRecMgr();
        recmgr->printStatus();
    }
    bool validateStructure() {
        return true;
    }
    void printObjectSizes() {
        std::cout<<"sizes: node="
                 <<(sizeof(Node<K, V>))
                 <<std::endl;
    }
    // try to clean up: must only be called by a single thread as part of the test harness!
    void debugGCSingleThreaded() {
        ds->debugGetRecMgr()->debugGCSingleThreaded();
    }

#ifdef USE_TREE_STATS
    class NodeHandler {
    public:
        typedef Node<K,V> * NodePtrType;
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
                leftDone = (node->left == NULL);
                rightDone = (node->right == NULL);
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
            return (node->left == NULL) && (node->right == NULL);
        }
        static size_t getNumChildren(NodePtrType node) {
            return (node->left != NULL) + (node->right != NULL);
        }
        static size_t getNumKeys(NodePtrType node) {
            // if (!isLeaf(node)) return 0;
            // if (node->key == KEY_RESERVED) return 0;
            return 1;
        }
        static size_t getSumOfKeys(NodePtrType node) {
            // if (!isLeaf(node)) return 0;
            // if (node->key == KEY_RESERVED) return 0;
            return (size_t) node->key;
        }
        static ChildIterator getChildIterator(NodePtrType node) {
            return ChildIterator(node);
        }
    };
    TreeStats<NodeHandler> * createTreeStats(const K& _minKey, const K& _maxKey) {
        return new TreeStats<NodeHandler>(new NodeHandler(_minKey, _maxKey), ds->debug_getEntryPoint()->left, true);
    }
#endif
};

#endif
