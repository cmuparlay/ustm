#pragma once


#include <cassert>
#include "phytm2.h"
#include "user_gstats_handler.h"

#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>


using namespace std;

#define PADDING_BYTES 128
#define MAX_PATH_SIZE 32
#ifndef MAX_THREADS_POW2
#define MAX_THREADS_POW2 512
#endif

#define RECLAIM_NODE(node) recmgr->retire(tid, (nodeptr) (node))
#define DEALLOCATE_NODE(node) recmgr->deallocate(tid, (nodeptr) (node))

#define tmarraycopy(src, srcStart, dest, destStart, len)                 \
    for (int ___i = 0; ___i < (len); ++___i) {                           \
        (dest)[(destStart) + ___i] = (src)[(srcStart) + ___i].load(); \
    }

#define arraycopy(src, srcStart, dest, destStart, len)         \
    for (int ___i = 0; ___i < (len); ++___i) {                 \
        (dest)[(destStart) + ___i] = (src)[(srcStart) + ___i]; \
    }

template <typename K>
struct kvpair {
    K key;
    void * val;
    kvpair() {}
};

template <typename K, class Compare>
int kv_compare(const void *_a, const void *_b) {
    const kvpair<K> *a = (const kvpair<K> *)_a;
    const kvpair<K> *b = (const kvpair<K> *)_b;
    static Compare cmp;
    return cmp(a->key, b->key) ? -1 : (cmp(b->key, a->key) ? 1 : 0);
}

template <typename K, typename V, int DEGREE, tx_safety mode = SAFE>
class Node {
  public:
    K searchKey;    // key that can be used to find this node (even if its empty) // const
    int weight;     // 0 or 1 // const
    bool leaf;      // 0 or 1 // const
    tx_field<mode, int                      > size;         // DEGREE of node
    tx_field<mode, K                        > keys[DEGREE];
    tx_field<mode, Node<K,V,DEGREE,mode> *  > ptrs[DEGREE]; // also doubles as a spot for VALUES
};
#define nodeptr Node<K,V,DEGREE> *
#define nodeptr_unsafe Node<K,V,DEGREE,UNSAFE> *

enum RetCode : int {
    RETRY = 0,
    UNNECCESSARY = 0,
    FAILURE = -1,
    SUCCESS = 1,
};

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
class ABTreeTM {
  public:
    volatile char padding0[PADDING_BYTES];
    void * const NO_VALUE;

  private:
    struct SearchInfo {
        nodeptr oNode;
        nodeptr oParent;
        nodeptr oGParent;
        int parentIndex = 0;
        int nodeIndex = 0;
        int keyIndex = 0;
        V val;
    };

    volatile char padding1[PADDING_BYTES];
    const int numThreads;
    const int a;
    const int b;
    K maxKey;
    volatile char padding2[PADDING_BYTES];
    nodeptr entry;
    volatile char padding3[PADDING_BYTES];
    RecordManager * const recmgr;
    volatile char padding4[PADDING_BYTES];
    Compare compare;
    volatile char padding5[PADDING_BYTES];

  public:
    ABTreeTM(const int _numThreads, const K anyKey, const K _maxKey);
    ~ABTreeTM();
    bool contains(const int tid, const K &key);
    V tryInsert(const int tid, const K &key, const V &value);
    V tryErase(const int tid, const K &key);
    V find(const int tid, const K &key);
    void printDebuggingDetails();
    nodeptr_unsafe getRoot();
    void initThread(const int tid);
    void deinitThread(const int tid);
    bool validate();

  private:
    int getKeyCount(nodeptr node);
    int getChildIndex(nodeptr node, const K &key);
    int getKeyIndex(nodeptr node, const K &key);
    nodeptr createInternalNode(const int tid, bool weight, int size, K searchKey);
    nodeptr createExternalNode(const int tid, bool weight, int size, K searchKey);
    void freeSubtree(const int tid, nodeptr_unsafe node);
    long validateSubtree(nodeptr_unsafe node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound);
    int erase(const int tid, SearchInfo &info, const K &key);
    int insert(const int tid, SearchInfo &info, const K &key, const V &value);
    V searchBasic(const int tid, const K &key);
    int search(const int tid, SearchInfo &info, const K &key, nodeptr target = NULL);
    int searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key);
    int fixDegreeViolation(const int tid, nodeptr viol);
    int fixWeightViolation(const int tid, nodeptr viol);
};

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::getKeyCount(nodeptr node) {
    return node->leaf ? node->size : node->size - 1;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::getChildIndex(nodeptr node, const K &key) {
    int nkeys = getKeyCount(node);
    int retval = 0;
    while (retval < nkeys && !compare(key, (const K &)node->keys[retval])) {
        ++retval;
    }
    return retval;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::getKeyIndex(nodeptr node, const K &key) {
    int retval = 0;
    while (retval < DEGREE && node->keys[retval] != key) {
        ++retval;
    }
    return retval;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
nodeptr ABTreeTM<RecordManager, K, V, DEGREE, Compare>::createInternalNode(const int tid, bool weight, int size, K searchKey) {
    nodeptr node = recmgr->template allocate<Node<K,V,DEGREE,SAFE>>(tid);
    node->leaf = 0;
    node->weight = weight;
    node->size = size;
    node->searchKey = searchKey;
    for (int i=0;i<DEGREE;++i) node->keys[i] = (K) 0;
    return node;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
nodeptr ABTreeTM<RecordManager, K, V, DEGREE, Compare>::createExternalNode(const int tid, bool weight, int size, K searchKey) {
    nodeptr node = createInternalNode(tid, weight, size, searchKey);
    node->leaf = 1;
    return node;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
ABTreeTM<RecordManager, K, V, DEGREE, Compare>::ABTreeTM(const int _numThreads, const K anyKey, const K _maxKey) : numThreads(_numThreads), recmgr(new RecordManager(numThreads)),
                                                                                                                       b(DEGREE), a(max(DEGREE / 4, 2)),
                                                                                                                       NO_VALUE((void *)(uintptr_t)0), maxKey(_maxKey) {
    assert(sizeof(V) == sizeof(nodeptr));
    assert(SUCCESS == RetCode::SUCCESS);
    assert(RETRY == RetCode::RETRY);

    compare = Compare();

    const int tid = 0;
    initThread(tid);

    // initial tree: entry is a sentinel node (with one pointer and no keys)
    //               that points to an empty node (no pointers and no keys)
    TM_BEGIN();
    nodeptr _entryLeft = createExternalNode(tid, true, 0, anyKey);

    //sentinel node
    nodeptr _entry = createInternalNode(tid, true, 1, anyKey);
    _entry->ptrs[0] = _entryLeft;

    entry = _entry;
    TM_END();
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
ABTreeTM<RecordManager, K, V, DEGREE, Compare>::~ABTreeTM() {
    int tid = 0;
    initThread(tid);
    freeSubtree(0, entry);
    deinitThread(tid);
    delete recmgr;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline nodeptr_unsafe ABTreeTM<RecordManager, K, V, DEGREE, Compare>::getRoot() {
    return (nodeptr_unsafe) entry;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeTM<RecordManager, K, V, DEGREE, Compare>::initThread(const int tid) {
    recmgr->initThread(tid);
    TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeTM<RecordManager, K, V, DEGREE, Compare>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline bool ABTreeTM<RecordManager, K, V, DEGREE, Compare>::contains(const int tid, const K &key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN_RO();
    auto retval = searchBasic(tid, key) != NO_VALUE;
    TM_END();
    return retval;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeTM<RecordManager, K, V, DEGREE, Compare>::find(const int tid, const K &key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN_RO();
    auto retval = searchBasic(tid, key);
    TM_END();
    return retval;
}

/* searchBasic(const int tid, const K &key)
 * Basic search, returns respective value associated with key, or NO_VALUE if nothing is found
 * does not return any path information like other searches (and is therefore slightly faster)
 * called by contains() and find()
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeTM<RecordManager, K, V, DEGREE, Compare>::searchBasic(const int tid, const K &key) {
    while (true) {
        nodeptr node = entry->ptrs[0];
        while (!node->leaf) node = node->ptrs[getChildIndex(node, key)];
        int keyIndex = getKeyIndex(node, key);
        if (keyIndex < DEGREE) return (V) node->ptrs[keyIndex];
        else return NO_VALUE;
    }
}

/* search(const int tid, SearchInfo &info, const K &key)
 * normal search used to search for a specific key, fills a SearchInfo struct so the caller
 * can manipulate the nodes around the searched for key
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::search(const int tid, SearchInfo &info, const K &key, nodeptr target) {
    info.oGParent = NULL;
    info.oParent = entry;
    info.nodeIndex = 0;
    info.oNode = entry->ptrs[0];
    while (!info.oNode->leaf && (target ? info.oNode != target : true)) {
        info.oGParent = info.oParent;
        info.oParent = info.oNode;
        info.parentIndex = info.nodeIndex;
        info.nodeIndex = getChildIndex(info.oNode, key);
        info.oNode = info.oNode->ptrs[info.nodeIndex];
    }
    if (target) {
        if (info.oNode == target) return RetCode::SUCCESS;
        return RetCode::FAILURE;
    } else {
        info.keyIndex = getKeyIndex(info.oNode, key);
        // if key found
        if (info.keyIndex < DEGREE) {
            info.val = (V) info.oNode->ptrs[info.keyIndex];
            return RetCode::SUCCESS;
        }
        return RetCode::FAILURE;
    }
}

/* searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key)
 * Searches for a key, however halts when a specific target node is reached. Return
 * is dependent on if the node halted at is the target (indicating the key searched for leads, at some point,
 * to this node)
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key) {
    return search(tid, info, key, target);

    // auto &path = paths[tid].path;
    // nodeptr node;

    // while (true) {
    //     path[0].node = entry;
    //     path[0].oVNumMark = entry->vNumMark;
    //     SOFTWARE_BARRIER;
    //     node = entry->ptrs[0];

    //     int currSize = 1;

    //     while (!node->leaf && node != target) {
    //         assert(currSize < MAX_PATH_SIZE - 1);
    //         path[currSize].node = node;
    //         path[currSize].oVNumMark = node->vNumMark;
    //         SOFTWARE_BARRIER;
    //         currSize++;

    //         info.parentIndex = info.nodeIndex;
    //         info.nodeIndex = getChildIndex(node, key);

    //         node = node->ptrs[info.nodeIndex];
    //     }

    //     assert(currSize < MAX_PATH_SIZE - 1);
    //     path[currSize].node = node;
    //     path[currSize].oVNumMark = node->vNumMark;
    //     SOFTWARE_BARRIER;
    //     info.oNode = path[currSize];
    //     currSize++;
    //     info.keyIndex = -1;

    //     info.oParent = path[currSize - 2];
    //     info.oNode = path[currSize - 1];

    //     if (currSize > 2) {
    //         info.oGParent = path[currSize - 3];
    //     } else {
    //         //could have found a grandparent on a previous iteration then failed path validation, don't want to use it for ops now!
    //         info.oGParent.reset();
    //     }

    //     if (node == target)
    //         return RetCode::SUCCESS;
    //     else if (validatePath(tid, currSize, path))
    //         return RetCode::FAILURE;
    // }

    // assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeTM<RecordManager, K, V, DEGREE, Compare>::tryInsert(const int tid, const K &key, const V &value) {
    SearchInfo info;
    while (true) {
        auto guard = recmgr->getGuard(tid);
        TM_BEGIN();
        int res = search(tid, info, key);
        if (res == RetCode::SUCCESS) {
            TM_END();
            return info.val;
        }
        if (insert(tid, info, key, value)) {
            TM_END();
            return NO_VALUE;
        }
        TM_END();
        // TM_RESTART();
    }
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::insert(const int tid, SearchInfo &info, const K &key, const V &value) {
    auto node = info.oNode;
    auto parent = info.oParent;
    assert(node->leaf);
    assert(!parent->leaf);
    int currSize = node->size;
    if (currSize < b) {
        //we have the capacity to fit this new key
        // find empty slot
        for (int i = 0; i < DEGREE; ++i) {
            if (node->keys[i] == (K) 0) {
                node->keys[i] = key;
                node->ptrs[i] = (nodeptr) value;
                auto oldSize = node->size;
                node->size = 1+oldSize;
                fixDegreeViolation(tid, node);
                return RetCode::SUCCESS;
            }
        }
        assert(false); // SHOULD NEVER HAPPEN
        return RetCode::RETRY;
    } else {
        //OVERFLOW
        //we do not have room for this key, we need to make new nodes so it fits
        // first, we create a std::pair of large arrays
        // containing too many keys and pointers to fit in a single node
        kvpair<K> tosort[DEGREE + 1];
        int k = 0;
        for (int i = 0; i < DEGREE; i++) {
            if (node->keys[i]) {
                tosort[k].key = node->keys[i];
                tosort[k].val = node->ptrs[i];
                ++k;
            }
        }
        tosort[k].key = key;
        tosort[k].val = value;
        ++k;
        qsort(tosort, k, sizeof(kvpair<K>), kv_compare<K, Compare>);

        // create new node(s):
        // since the new arrays are too big to fit in a single node,
        // we replace l by a new subtree containing three new nodes:
        // a parent, and two leaves;
        // the array contents are then split between the two new leaves

        const int leftSize = k / 2;
        auto left = createExternalNode(tid, true, leftSize, tosort[0].key);
        for (int i = 0; i < leftSize; i++) {
            left->keys[i] = tosort[i].key;
            left->ptrs[i] = (nodeptr)tosort[i].val;
        }

        const int rightSize = (DEGREE + 1) - leftSize;
        auto right = createExternalNode(tid, true, rightSize, tosort[leftSize].key);
        for (int i = 0; i < rightSize; i++) {
            right->keys[i] = tosort[i + leftSize].key;
            right->ptrs[i] = (nodeptr)tosort[i + leftSize].val;
        }

        auto replacementNode = createInternalNode(tid, parent == entry, 2, tosort[leftSize].key);
        replacementNode->keys[0] = tosort[leftSize].key;
        replacementNode->ptrs[0] = left;
        replacementNode->ptrs[1] = right;

        // note: weight of new internal node n will be zero,
        //       unless it is the root; this is because we test
        //       p == entry, above; in doing this, we are actually
        //       performing Root-Zero at the same time as this Overflow
        //       if n will become the root

        parent->ptrs[info.nodeIndex] = replacementNode;
        // RECLAIM_NODE(node);
        fixWeightViolation(tid, replacementNode);
        return RetCode::SUCCESS;
        // DEALLOCATE_NODE(replacementNode);
        // DEALLOCATE_NODE(right);
        // DEALLOCATE_NODE(left);
        // return RetCode::RETRY;
    }
    // }
    assert(false);
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeTM<RecordManager, K, V, DEGREE, Compare>::tryErase(const int tid, const K &key) {
    SearchInfo info;
    while (true) {
        auto guard = recmgr->getGuard(tid);
        TM_BEGIN();
        int res = search(tid, info, key);
        if (res == RetCode::FAILURE) {
            TM_END();
            return NO_VALUE;
        }
        if (erase(tid, info, key)) {
            TM_END();
            return info.val;
        }
        TM_END();
        // TM_RESTART();
    }
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::erase(const int tid, SearchInfo &info, const K &key) {
    auto node = info.oNode;
    auto parent = info.oParent;
    auto gParent = info.oGParent;
    assert(node->leaf);
    assert(!parent->leaf);
    assert(gParent == NULL || !gParent->leaf);
    int currSize = node->size;
    node->keys[info.keyIndex] = (K) 0;
    node->size = currSize-1;
    fixDegreeViolation(tid, node);
    return RetCode::SUCCESS;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::fixWeightViolation(const int tid, nodeptr viol) {
    while (true) {
        /**these checks now need to happen every loop, as these fields are no longer immutable, hence the state of the node can change
         * In addition, version number changes do not indicate that the some other thread is responsible
         * Hence, we must loop until the issue is resolved. What is key here is that if you create a violation you will observe it, and will not
         * be able to leave until it is resolved, and since no thread can leave until there is a time *after* their update that the node will be
         * fixed, so they must fix, or observe the fix, of the invalidation they created.
         **/

        // assert: viol is internal (because leaves always have weight = 1)
        // assert: viol is not entry or root (because both always have weight = 1)
        if (viol->weight) {
            return RetCode::UNNECCESSARY;
        }

        SearchInfo info;
        searchTarget(tid, info, viol, viol->searchKey);

        auto node = info.oNode;
        auto parent = info.oParent;
        auto gParent = info.oGParent;

        if (node != viol) {
            // viol was replaced by another update.
            // we hand over responsibility for viol to that update.
            return RetCode::UNNECCESSARY;
        }

        // we cannot apply this update if p has a weight violation
        // so, we check if this is the case, and, if so, try to fix it
        if (!parent->weight) {
            fixWeightViolation(tid, parent);
            continue;
        }

        const int psize = parent->size;
        const int nsize = viol->size;
        const int c = psize + nsize;
        const int size = c - 1;

        if (size <= b) {
            assert(!node->leaf);
            /**
             * Absorb
             */

            // create new node(s)
            // the new arrays are small enough to fit in a single node,
            // so we replace p by a new internal node.
            nodeptr absorber = createInternalNode(tid, true, size, (K)0x0);
            tmarraycopy(parent->ptrs, 0, absorber->ptrs, 0, info.nodeIndex);
            tmarraycopy(node->ptrs, 0, absorber->ptrs, info.nodeIndex, nsize);
            tmarraycopy(parent->ptrs, info.nodeIndex + 1, absorber->ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));

            tmarraycopy(parent->keys, 0, absorber->keys, 0, info.nodeIndex);
            tmarraycopy(node->keys, 0, absorber->keys, info.nodeIndex, getKeyCount(node));
            tmarraycopy(parent->keys, info.nodeIndex, absorber->keys, info.nodeIndex + getKeyCount(node), getKeyCount(parent) - info.nodeIndex);
            //hate this
            absorber->searchKey = absorber->keys[0]; // TODO: verify this is same as in llx/scx abtree

            gParent->ptrs[info.parentIndex] = absorber;
            // RECLAIM_NODE(node);
            // RECLAIM_NODE(parent);

            fixDegreeViolation(tid, absorber);
            return RetCode::SUCCESS;
            // DEALLOCATE_NODE(absorber);
        } else {
            assert(!node->leaf);
            /**
             * Split
             */

            // merge keys of p and l into one big array (and similarly for children)
            // (we essentially replace the pointer to l with the contents of l)
            K keys[2 * DEGREE];
            nodeptr ptrs[2 * DEGREE];
            tmarraycopy(parent->ptrs, 0, ptrs, 0, info.nodeIndex);
            tmarraycopy(node->ptrs, 0, ptrs, info.nodeIndex, nsize);
            tmarraycopy(parent->ptrs, info.nodeIndex + 1, ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));
            tmarraycopy(parent->keys, 0, keys, 0, info.nodeIndex);
            tmarraycopy(node->keys, 0, keys, info.nodeIndex, getKeyCount(node));
            tmarraycopy(parent->keys, info.nodeIndex, keys, info.nodeIndex + getKeyCount(node), getKeyCount(parent) - info.nodeIndex);

            // the new arrays are too big to fit in a single node,
            // so we replace p by a new internal node and two new children.
            //
            // we take the big merged array and split it into two arrays,
            // which are used to create two new children u and v.
            // we then create a new internal node (whose weight will be zero
            // if it is not the root), with u and v as its children.

            // create new node(s)
            const int leftSize = size / 2;
            auto left = createInternalNode(tid, true, leftSize, keys[0]);
            arraycopy(keys, 0, left->keys, 0, leftSize - 1);
            arraycopy(ptrs, 0, left->ptrs, 0, leftSize);

            const int rightSize = size - leftSize;
            auto right = createInternalNode(tid, true, rightSize, keys[leftSize]);
            arraycopy(keys, leftSize, right->keys, 0, rightSize - 1);
            arraycopy(ptrs, leftSize, right->ptrs, 0, rightSize);

            // note: keys[leftSize - 1] should be the same as n->keys[0]
            auto newNode = createInternalNode(tid, gParent == entry, 2, keys[leftSize - 1]);
            newNode->keys[0] = keys[leftSize - 1];
            newNode->ptrs[0] = left;
            newNode->ptrs[1] = right;

            // note: weight of new internal node n will be zero,
            //       unless it is the root; this is because we test
            //       gp == entry, above; in doing this, we are actually
            //       performing Root-Zero at the same time as this Overflow
            //       if n will become the root

            gParent->ptrs[info.parentIndex] = newNode;
            // RECLAIM_NODE(node);
            // RECLAIM_NODE(parent);

            fixWeightViolation(tid, newNode);
            fixDegreeViolation(tid, newNode);

            return RetCode::SUCCESS;
            // DEALLOCATE_NODE(left);
            // DEALLOCATE_NODE(right);
            // DEALLOCATE_NODE(newNode);
        }
    }
    assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeTM<RecordManager, K, V, DEGREE, Compare>::fixDegreeViolation(const int tid, nodeptr viol) {
    // we search for viol and try to fix any violation we find there
    // this entails performing AbsorbSibling or Distribute.

    nodeptr parent;
    nodeptr gParent;
    nodeptr node = NULL;
    nodeptr sibling = NULL;
    nodeptr left = NULL;
    nodeptr right = NULL;

    while (true) {
        //see the monster comment in fixWeightViolation as to why these checks happen in the loop now
        if (viol->size >= a || viol == entry || viol == entry->ptrs[0]) {
            return RetCode::UNNECCESSARY; // no degree violation at viol
        }

        /**
         * search for viol
         */
        SearchInfo info;
        searchTarget(tid, info, viol, viol->searchKey);
        node = info.oNode;
        parent = info.oParent;
        gParent = info.oGParent;

        if (node != viol) {
            // viol was replaced by another update.
            // we hand over responsibility for viol to that update.
            return RetCode::UNNECCESSARY;
        }

        // assert: gp != NULL (because if AbsorbSibling or Distribute can be applied, then p is not the root)
        int siblingIndex = (info.nodeIndex > 0 ? info.nodeIndex - 1 : 1);
        sibling = parent->ptrs[siblingIndex];

        // we can only apply AbsorbSibling or Distribute if there are no
        // weight violations at p, l or s.
        // so, we first check for any weight violations,
        // and fix any that we see.
        bool foundWeightViolation = false;
        if (!parent->weight) {
            foundWeightViolation = true;
            fixWeightViolation(tid, parent);
        }

        if (!node->weight) {
            foundWeightViolation = true;
            fixWeightViolation(tid, node);
        }

        if (!sibling->weight) {
            foundWeightViolation = true;
            fixWeightViolation(tid, sibling);
        }
        // if we see any weight violations, then either we fixed one,
        // removing one of these nodes from the tree,
        // or one of the nodes has been removed from the tree by another
        // rebalancing step, so we retry the search for viol
        if (foundWeightViolation) {
            continue;
        }

        // assert: there are no weight violations at p, l or s
        // assert: l and s are either both leaves or both internal nodes
        //         (because there are no weight violations at these nodes)

        // also note that p->size >= a >= 2

        int leftIndex;
        int rightIndex;
        nodeptr left = NULL;
        nodeptr right = NULL;

        if (info.nodeIndex < siblingIndex) {
            left = node;
            right = sibling;
            leftIndex = info.nodeIndex;
            rightIndex = siblingIndex;
        } else {
            left = sibling;
            right = node;
            leftIndex = siblingIndex;
            rightIndex = info.nodeIndex;
        }

        int lsize = left->size;
        int rsize = right->size;
        int psize = parent->size;
        int size = lsize+rsize;
        // assert(left->weight && right->weight); // or version # has changed

        if (size < 2 * a) {
            /**
             * AbsorbSibling
             */

            nodeptr newNode = NULL;
            // create new node(s))
            int keyCounter = 0, ptrCounter = 0;
            if (left->leaf) {
                //duplicate code can be cleaned up, but it would make it far less readable...
                nodeptr newNodeExt = createExternalNode(tid, true, size, node->searchKey);
                for (int i = 0; i < DEGREE; i++) {
                    if (left->keys[i]) {
                        newNodeExt->keys[keyCounter++] = left->keys[i].load();
                        newNodeExt->ptrs[ptrCounter++] = left->ptrs[i].load();
                    }
                }
                assert(right->leaf);
                for (int i = 0; i < DEGREE; i++) {
                    if (right->keys[i]) {
                        newNodeExt->keys[keyCounter++] = right->keys[i].load();
                        newNodeExt->ptrs[ptrCounter++] = right->ptrs[i].load();
                    }
                }
                newNode = newNodeExt;
            } else {
                nodeptr newNodeInt = createInternalNode(tid, true, size, node->searchKey);
                for (int i = 0; i < getKeyCount(left); i++) {
                    newNodeInt->keys[keyCounter++] = left->keys[i].load();
                }
                newNodeInt->keys[keyCounter++] = parent->keys[leftIndex].load();
                for (int i = 0; i < lsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = left->ptrs[i].load();
                }
                assert(!right->leaf);
                for (int i = 0; i < getKeyCount(right); i++) {
                    newNodeInt->keys[keyCounter++] = right->keys[i].load();
                }
                for (int i = 0; i < rsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = right->ptrs[i].load();
                }
                newNode = newNodeInt;
            }

            // now, we atomically replace p and its children with the new nodes.
            // if appropriate, we perform RootAbsorb at the same time.
            if (gParent == entry && psize == 2) {
                gParent->ptrs[info.parentIndex] = newNode;
                // RECLAIM_NODE(node);
                // RECLAIM_NODE(parent);
                // RECLAIM_NODE(sibling);
                fixDegreeViolation(tid, newNode);
                return RetCode::SUCCESS;
// cleanup_retry0:
//                 DEALLOCATE_NODE(newNode);
            } else {
                assert(gParent != entry || psize > 2);
                // create n from p by:
                // 1. skipping the key for leftindex and child pointer for ixToS
                // 2. replacing l with newl
                auto newParent = createInternalNode(tid, true, psize - 1, parent->searchKey);
                for (int i = 0; i < leftIndex; i++) {
                    newParent->keys[i] = parent->keys[i].load();
                }
                for (int i = 0; i < siblingIndex; i++) {
                    newParent->ptrs[i] = parent->ptrs[i].load();
                }
                for (int i = leftIndex + 1; i < getKeyCount(parent); i++) {
                    newParent->keys[i - 1] = parent->keys[i].load();
                }
                for (int i = info.nodeIndex + 1; i < psize; i++) {
                    newParent->ptrs[i - 1] = parent->ptrs[i].load();
                }

                // replace l with newl in n's pointers
                newParent->ptrs[info.nodeIndex - (info.nodeIndex > siblingIndex)] = newNode;

                gParent->ptrs[info.parentIndex] = newParent;
                // RECLAIM_NODE(node);
                // RECLAIM_NODE(parent);
                // RECLAIM_NODE(sibling);
                fixDegreeViolation(tid, newNode);
                fixDegreeViolation(tid, newParent);
                return RetCode::SUCCESS;
// cleanup_retry1:
//                 DEALLOCATE_NODE(newParent);
//                 DEALLOCATE_NODE(newNode);
            }

        } else {
            /**
             * Distribute
             */

            int leftSize = size / 2;
            int rightSize = size - leftSize;

            nodeptr newLeft = NULL;
            nodeptr newRight = NULL;

            kvpair<K> tosort[DEGREE + 1];

            // combine the contents of l and s (and one key from p if l and s are internal)

            int keyCounter = 0;
            int valCounter = 0;
            if (left->leaf) {
                assert(right->leaf);
                for (int i = 0; i < DEGREE; i++) {
                    if (left->keys[i]) {
                        tosort[keyCounter++].key = left->keys[i];
                        tosort[valCounter++].val = left->ptrs[i];
                    }
                }
            } else {
                for (int i = 0; i < getKeyCount(left); i++) {
                    tosort[keyCounter++].key = left->keys[i];
                }
                for (int i = 0; i < lsize; i++) {
                    tosort[valCounter++].val = left->ptrs[i];
                }
            }

            if (!left->leaf) tosort[keyCounter++].key = parent->keys[leftIndex];

            if (right->leaf) {
                for (int i = 0; i < DEGREE; i++) {
                    if (right->keys[i]) {
                        tosort[keyCounter++].key = right->keys[i];
                        tosort[valCounter++].val = right->ptrs[i];
                    }
                }
            } else {
                for (int i = 0; i < getKeyCount(right); i++) {
                    tosort[keyCounter++].key = right->keys[i];
                }
                for (int i = 0; i < rsize; i++) {
                    tosort[valCounter++].val = right->ptrs[i];
                }
            }

            if (left->leaf) qsort(tosort, keyCounter, sizeof(kvpair<K>), kv_compare<K, Compare>);

            keyCounter = 0;
            valCounter = 0;
            K pivot;

            if (left->leaf) {
                nodeptr newLeftExt = createExternalNode(tid, true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize; i++) {
                    newLeftExt->keys[i] = tosort[keyCounter++].key;
                    newLeftExt->ptrs[i] = (nodeptr)tosort[valCounter++].val;
                }
                newLeft = newLeftExt;
                newLeft->searchKey = newLeftExt->keys[0];
                pivot = tosort[keyCounter].key;

            } else {
                nodeptr newLeftInt = createInternalNode(tid, true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize - 1; i++) {
                    newLeftInt->keys[i] = tosort[keyCounter++].key;
                }
                for (int i = 0; i < leftSize; i++) {
                    newLeftInt->ptrs[i] = (nodeptr)tosort[valCounter++].val;
                }
                newLeft = newLeftInt;
                newLeft->searchKey = newLeftInt->keys[0];
                pivot = tosort[keyCounter++].key;
            }

            // reserve one key for the parent (to go between newleft and newright))

            if (right->leaf) {
                assert(left->leaf);
                auto newRightExt = createExternalNode(tid, true, rightSize, (K)0x0);
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    newRightExt->keys[i] = tosort[keyCounter++].key;
                }
                newRight = newRightExt;
                newRight->searchKey = newRightExt->keys[0]; // TODO: verify searchKey setting is same as llx/scx based version
                for (int i = 0; i < rightSize; i++) {
                    newRight->ptrs[i] = (nodeptr)tosort[valCounter++].val;
                }
            } else {
                nodeptr newRightInt = createInternalNode(tid, true, rightSize, (K)0x0);
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    newRightInt->keys[i] = tosort[keyCounter++].key;
                }
                newRight = newRightInt;
                newRight->searchKey = newRightInt->keys[0];
                for (int i = 0; i < rightSize; i++) {
                    newRight->ptrs[i] = (nodeptr)tosort[valCounter++].val;
                }
            }

            // in this case we replace the parent, despite not having to in the llx/scx version...
            // this is a holdover from kcas. experiments show this case almost never occurs, though, so perf impact is negligible.
            auto newParent = createInternalNode(tid, parent->weight, psize, parent->searchKey);
            tmarraycopy(parent->keys, 0, newParent->keys, 0, getKeyCount(parent));
            tmarraycopy(parent->ptrs, 0, newParent->ptrs, 0, psize);
            newParent->ptrs[leftIndex] = newLeft;
            newParent->ptrs[rightIndex] = newRight;
            newParent->keys[leftIndex] = pivot;

            gParent->ptrs[info.parentIndex] = newParent;
            // RECLAIM_NODE(node);
            // RECLAIM_NODE(sibling);
            // RECLAIM_NODE(parent);
            fixDegreeViolation(tid, newParent);
            return RetCode::SUCCESS;
// cleanup_retry2:
//             DEALLOCATE_NODE(newLeft);
//             DEALLOCATE_NODE(newRight);
//             DEALLOCATE_NODE(newParent);
        }
    }
    assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeTM<RecordManager, K, V, DEGREE, Compare>::printDebuggingDetails() {
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
long ABTreeTM<RecordManager, K, V, DEGREE, Compare>::validateSubtree(nodeptr_unsafe node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound) {

    // if (node == NULL) return 0;
    // // graph << "\"" << node << "\"" << "[label=\"K: " << node->searchKey << " - W: "
    // //         << node->weight << " - L: " << node->leaf << " - N: " << node << "\"];\n";
    // graph << "\"" << node << "\"" << "[shape=record, label=\"S" << node->searchKey << " | W"
    //         << node->weight << " | L" << node->leaf; //<< "\"];\n";
    // int last = -1;
    // if (node->leaf) {
    //     for (int i = 0; i < DEGREE; i++) {
    //         K key = node->keys[i];
    //         graph << " | <k"<<i<<">";
    //         if (key) graph << key; else graph << "x";
    //     }
    // } else {
    //     for (int i = 0; i < node->size-1; i++) {
    //         K key = node->keys[i];
    //         graph << " | <p"<<i<<">";
    //         graph << " | <k"<<i<<">";
    //         if (key) graph << key; else graph << "x";
    //     }
    //     graph << " | <p"<<(node->size-1)<<">";
    // }
    // graph << " \"];\n";

    // if (!node->weight) {
    //     log << "Weight Violation! " << node->searchKey << "\n";
    //     errorFound = true;
    // }

    // if (node->leaf) {
    //     for (int i = 0; i < DEGREE; i++) {
    //         auto leaf = node;
    //         K key = leaf->keys[i];
    //         if (key) {
    //             // graph << "\"" << node << "\" -> \"" << key << "\";\n";
    //             if (key < 0 || key > MAXKEY) {
    //                 log << "Suspected pointer in leaf! " << node->searchKey << "\n";
    //                 errorFound = true;
    //             }
    //             if (keys.count(key) > 0) {
    //                 log << "DUPLICATE KEY! " << node->searchKey << "\n";
    //                 errorFound = true;
    //             }
    //             keys.insert(key);
    //         }
    //     }
    // }

    // if (!node->leaf) {
    //     for (int i = 0; i < node->size; i++) {
    //         graph << "\"" << node << "\":<p"<<i<<"> -> \"" << node->ptrs[i] << "\";\n";
    //         validateSubtree(node->ptrs[i], keys, graph, log, errorFound);
    //     }
    // }

    return 1;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
bool ABTreeTM<RecordManager, K, V, DEGREE, Compare>::validate() {
    // fflush(stdout);
    // std::unordered_set<K> keys = {};
    // bool errorFound;

    // rename("graph.dot", "graph_before.dot");
    // ofstream graph;
    // graph.open("graph.dot");
    // graph << "digraph G {\n";

    // ofstream log;
    // log.open("log.txt", std::ofstream::out);

    // auto t = std::time(nullptr);
    // auto tm = *std::localtime(&t);
    // log << "Run at: " << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << "\n";

    // long ret = validateSubtree(getRoot(), keys, graph, log, errorFound);
    // graph << "}";
    // graph.flush();

    // graph.close();

    // if (!errorFound) {
    //     log << "Validated Successfully!\n";
    // }
    // log.flush();

    // log.close();
    // fflush(stdout);
    // return !errorFound;
    return true;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeTM<RecordManager, K, V, DEGREE, Compare>::freeSubtree(const int tid, nodeptr_unsafe node) {
    if (!node->leaf) {
        for (int i = 0; i < node->size; i++) {
            freeSubtree(tid, node->ptrs[i]);
        }
    }
    DEALLOCATE_NODE(node);
}
