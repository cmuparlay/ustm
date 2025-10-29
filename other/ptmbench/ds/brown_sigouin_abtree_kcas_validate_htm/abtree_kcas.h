#pragma once

#include <cassert>
#define MAX_KCAS 6
#include "kcas.h"
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

#define IS_MARKED(word) (word & 0x1)
#define TO_LEAF(node) ((NodeExternal<K, V, DEGREE> *)(node))
#define TO_INTERNAL(node) ((NodeInternal<K, V, DEGREE> *)(node))
#define TO_NODE(node) ((Node<K,V,DEGREE> *)(node))

#define RECLAIM_NODE(node) node->leaf ? recmgr->retire(tid, TO_LEAF((node))) : recmgr->retire(tid, TO_INTERNAL((node)))
#define DEALLOCATE_NODE(node) node->leaf ? recmgr->deallocate(tid, TO_LEAF((node))) : recmgr->deallocate(tid, TO_INTERNAL((node)))

//This is a bit dangerous, it obfuscates a lot of work
#define caswordarraycopy(src, srcStart, dest, destStart, len)            \
    for (int ___i = 0; ___i < (len); ++___i) {                           \
        (dest)[(destStart) + ___i].setInitVal((src)[(srcStart) + ___i]); \
    }

#define arraycopy(src, srcStart, dest, destStart, len)         \
    for (int ___i = 0; ___i < (len); ++___i) {                 \
        (dest)[(destStart) + ___i] = (src)[(srcStart) + ___i]; \
    }

template <typename K>
struct kvpair {
    K key;
    void *val;

    kvpair() {
    }
};

template <typename K, class Compare>
int kv_compare(const void *_a, const void *_b) {
    const kvpair<K> *a = (const kvpair<K> *)_a;
    const kvpair<K> *b = (const kvpair<K> *)_b;
    static Compare cmp;
    return cmp(a->key, b->key) ? -1
        : cmp(b->key, a->key) ? 1
        : 0;
}

template <typename K, typename V, int DEGREE>
class Node {
public:
    volatile bool leaf; // 0 or 1
    casword<casword_t> vNumMark;
    volatile int weight;  // 0 or 1
    casword<int> size;    // DEGREE of node
    casword<K> searchKey; // DEGREE of node
};

template <typename K, typename V, int DEGREE>
class NodeInternal : public Node<K, V, DEGREE> {
public:
    K keys[DEGREE];
    casword<Node<K, V, DEGREE> *> ptrs[DEGREE];
};

template <typename K, typename V, int DEGREE>
class NodeExternal : public Node<K, V, DEGREE> {
public:
    casword<K> keys[DEGREE];
    casword<Node<K, V, DEGREE> *> ptrs[DEGREE];
};

enum RetCode : int {
    RETRY = 0,
    UNNECCESSARY = 0,
    FAILURE = -1,
    SUCCESS = 1,
};

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
class ABTreeKCAS {
public:
    volatile char padding0[PADDING_BYTES];
    void *const NO_VALUE;

private:

    struct SearchInfo {
        Node<K, V, DEGREE> * node;
        casword_t nVer;
        int nodeIndex = 0;
        Node<K, V, DEGREE> * parent = NULL;
        casword_t pVer = 0;
        int parentIndex = 0;
        Node<K, V, DEGREE> * gParent = NULL;
        casword_t gVer = 0;
        int keyIndex = 0;
        V val;

        void reset() {
            gVer = 0;
            gParent = NULL;
        }
    };

    volatile char padding1[PADDING_BYTES];
    const int numThreads;
    const int a;
    const int b;
    const bool ALLOW_ONE_EXTRA_SLACK_PER_NODE;
    K maxKey;
    volatile char padding2[PADDING_BYTES];
    NodeInternal<K, V, DEGREE> *entry;
    volatile char padding3[PADDING_BYTES];
    RecordManager *const recmgr;
    volatile char padding4[PADDING_BYTES];
    Compare compare;
    volatile char padding5[PADDING_BYTES];

public:
    ABTreeKCAS(const int _numThreads, const K anyKey, const K _maxKey);

    ~ABTreeKCAS();

    bool contains(const int tid, const K &key);

    V tryInsert(const int tid, const K &key, const V &value);

    V tryErase(const int tid, const K &key);

    V find(const int tid, const K &key);

    void printDebuggingDetails();

    Node<K, V, DEGREE> *getRoot();

    void initThread(const int tid);

    void deinitThread(const int tid);

    bool validate();

private:
    int getKeyCount(Node<K, V, DEGREE> *node);

    int getChildIndex(NodeInternal<K, V, DEGREE> *node, const K &key);

    int getKeyIndex(NodeExternal<K, V, DEGREE> *node, const K &key);

    NodeInternal<K, V, DEGREE> *createInternalNode(const int tid, bool weight, int size, K searchKey);

    NodeExternal<K, V, DEGREE> *createExternalNode(const int tid, bool weight, int size, K searchKey);

    void freeSubtree(const int tid, Node<K, V, DEGREE> *node);

    long validateSubtree(Node<K, V, DEGREE> *node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound);

    int erase(const int tid, SearchInfo &info, const K &key);

    int insert(const int tid, SearchInfo &info, const K &key, const V &value);

    V searchBasic(const int tid, const K &key);

    int search(const int tid, SearchInfo &info, const K &key);

    int searchTarget(const int tid, SearchInfo &info, Node<K, V, DEGREE> *target, const K &key);

    bool compareKeys(const K &first, const K &second);

    int fixDegreeViolation(const int tid, Node<K, V, DEGREE> *viol);

    int fixWeightViolation(const int tid, Node<K, V, DEGREE> *viol);
};

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::getKeyCount(Node<K, V, DEGREE> *node) {
    return node->leaf ? node->size : node->size - 1;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::getChildIndex(NodeInternal<K, V, DEGREE> *node, const K &key) {
    int nkeys = getKeyCount((Node<K, V, DEGREE> *)node);
    int retval = 0;
    while (retval < nkeys && !compareKeys(key, (const K &)node->keys[retval])) {
        ++retval;
    }
    return retval;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::getKeyIndex(NodeExternal<K, V, DEGREE> *node, const K &key) {
    int retval = 0;
    while (retval < DEGREE && node->keys[retval] != key) {
        ++retval;
    }
    return retval;
}


template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline bool ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::compareKeys(const K &first, const K &second) {
    return compare(first, second);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
NodeInternal<K, V, DEGREE> *ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::createInternalNode(const int tid, bool weight, int size, K searchKey) {
    NodeInternal<K, V, DEGREE> *node = recmgr->template allocate<NodeInternal<K, V, DEGREE>>(tid);
    node->vNumMark.setInitVal(0);
    node->leaf = 0;              // 0 or 1
    node->weight = weight;       // 0 or 1
    node->size.setInitVal(size); // DEGREE of node
    node->searchKey.setInitVal(searchKey);
    for (int i=0;i<DEGREE;++i) {
        node->keys[i] = (K)0;
        // node->ptrs[i] = NO_VALUE;
    }
    return node;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
NodeExternal<K, V, DEGREE> *ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::createExternalNode(const int tid, bool weight, int size, K searchKey) {
    NodeExternal<K, V, DEGREE> *node = recmgr->template allocate<NodeExternal<K, V, DEGREE>>(tid);
    node->vNumMark.setInitVal(0);
    node->leaf = 1;              // 0 or 1
    node->weight = weight;       // 0 or 1
    node->size.setInitVal(size); // DEGREE of node
    node->searchKey.setInitVal(searchKey);
    for (int i=0;i<DEGREE;++i) {
        node->keys[i].setInitVal((K)0);
        // node->ptrs[i] = NO_VALUE;
    }
    return node;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::ABTreeKCAS(const int _numThreads, const K anyKey, const K _maxKey) : numThreads(_numThreads), recmgr(new RecordManager(numThreads)),
ALLOW_ONE_EXTRA_SLACK_PER_NODE(true), b(DEGREE), a(max(DEGREE / 4, 2)),
NO_VALUE((void *)(uintptr_t)0), maxKey(_maxKey) {
    assert(sizeof(V) == sizeof(Node<K, V, DEGREE> *));
    assert(SUCCESS == RetCode::SUCCESS);
    assert(RETRY == RetCode::RETRY);

    compare = Compare();

    const int tid = 0;
    initThread(tid);

    // initial tree: entry is a sentinel node (with one pointer and no keys)
    //               that points to an empty node (no pointers and no keys)
    auto _entryLeft = createExternalNode(tid, true, 0, anyKey);

    //sentinel node
    auto _entry = createInternalNode(tid, true, 1, anyKey);
    _entry->ptrs[0].setInitVal((Node<K, V, DEGREE> *)_entryLeft);

    entry = _entry;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::~ABTreeKCAS() {
    int tid = 0;
    initThread(tid);
    freeSubtree(0, entry);
    deinitThread(tid);
    delete recmgr;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline Node<K, V, DEGREE> *ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::getRoot() {
    return (Node<K, V, DEGREE> *)entry;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::initThread(const int tid) {
    recmgr->initThread(tid);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
inline bool ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::contains(const int tid, const K &key) {
    while (true) {
        auto guard = recmgr->getGuard(tid);
        kcas::start();
        if (searchBasic(tid, key) == NO_VALUE) {
            if (kcas::validate()) return false;
        }
        else return true;
    }
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::find(const int tid, const K &key) {
    while (true) {
        auto guard = recmgr->getGuard(tid);
        kcas::start();
        auto res = searchBasic(tid, key);
        if (res == NO_VALUE) {
            if (kcas::validate()) return res;
        }
        else return res;
    }
}

/* searchBasic(const int tid, const K &key)
 * Basic search, returns respective value associated with key, or NO_VALUE if nothing is found
 * does not return any path information like other searches (and is therefore slightly faster)
 * called by contains() and find()
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::searchBasic(const int tid, const K &key) {

    Node<K, V, DEGREE> *node;

    GSTATS_ADD(tid, abtree_internal_basic_search_while_iters, 1);
    kcas::visit((Node<K, V, DEGREE> *) entry);
    SOFTWARE_BARRIER;
    node = entry->ptrs[0];

    while (!node->leaf) {
        kcas::visit(node);

        SOFTWARE_BARRIER;

        auto intNode = TO_INTERNAL(node);
        node = intNode->ptrs[getChildIndex(intNode, key)];
    }

    kcas::visit(node);

    SOFTWARE_BARRIER;

    auto extNode = TO_LEAF(node);

    int keyIndex = getKeyIndex(extNode, key);
    if (keyIndex < DEGREE) {
        V val = (V)extNode->ptrs[keyIndex];
        if (extNode->keys[keyIndex] == key) {
            return val;
        }

        //one of the above conditions (index not in bounds or key is not exact)
        //so we need to check to make sure that it doesn't exist in truth by validating
    }
    return NO_VALUE;
}

/* search(const int tid, SearchInfo &info, const K &key)
 * normal search used to search for a specific key, fills a SearchInfo struct so the caller
 * can manipulate the nodes around the searched for key
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::search(const int tid, SearchInfo &info, const K &key) {

    while (true) {
        GSTATS_ADD(tid, abtree_internal_complex_search_while_iters, 1);
        info.parent = (Node<K, V, DEGREE> *)entry;
        info.pVer = kcas::visit(info.parent);

        SOFTWARE_BARRIER;
        info.node = entry->ptrs[0];
        info.nVer = kcas::visit(info.node);
        while (!info.node->leaf) {
            info.gParent = info.parent;
            info.gVer = info.pVer;
            info.parent = info.node;
            info.parentIndex = info.nodeIndex;
            info.pVer = info.nVer;

            SOFTWARE_BARRIER;
            info.nodeIndex = getChildIndex(TO_INTERNAL(info.node), key);
            info.node = ((NodeInternal<K, V, DEGREE> *)info.node)->ptrs[info.nodeIndex];
            info.nVer = kcas::visit(info.node);
        }
        SOFTWARE_BARRIER;
        // if (IS_MARKED(path[currSize].oVNumMark)) continue;

        auto extNode = TO_LEAF(info.node);
        info.keyIndex = getKeyIndex(extNode, key);

        if (info.keyIndex < DEGREE) {
            info.val = (V)extNode->ptrs[info.keyIndex];

            if (extNode->keys[info.keyIndex] == key) {
                //sure we found the value and key, but it is possible that
                //they are not a pair (no longer have certainty that a node
                //we are reading will not change with KCAS)...
                if (extNode->vNumMark != info.nVer)
                    continue;
                return RetCode::SUCCESS;
            }
        }

        //one of the above conditions (index not in bounds or key is not exact)
        //so we need to check to make sure that it doesn't exist in truth by validating


        info.val = NO_VALUE;
        return RetCode::FAILURE;
    }
    assert(false);
}

/* searchTarget(const int tid, SearchInfo &info, Node<K,V,DEGREE> * target, const K &key)
 * Searches for a key, however halts when a specific target node is reached. Return
 * is dependent on if the node halted at is the target (indicating the key searched for leads, at some point,
 * to this node)
 */
template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::searchTarget(const int tid, SearchInfo &info, Node<K, V, DEGREE> *target, const K &key) {

    GSTATS_ADD(tid, abtree_internal_complex_search_while_iters, 1);
    info.parent = (Node<K, V, DEGREE> *)entry;
    info.pVer = kcas::visit(info.parent);

    SOFTWARE_BARRIER;
    info.node = entry->ptrs[0];
    info.nVer = kcas::visit(info.node);
    while (!info.node->leaf && info.node != target) {
        info.gParent = info.parent;
        info.gVer = info.pVer;
        info.parent = info.node;
        info.parentIndex = info.nodeIndex;
        info.pVer = info.nVer;

        SOFTWARE_BARRIER;
        info.nodeIndex = getChildIndex(TO_INTERNAL(info.node), key);
        info.node = ((NodeInternal<K, V, DEGREE> *)info.node)->ptrs[info.nodeIndex];
        info.nVer = kcas::visit(info.node);
    }
    SOFTWARE_BARRIER;
    info.keyIndex = -1;
    if (info.node == target)
        return RetCode::SUCCESS;
    else
        return RetCode::FAILURE;

    assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::tryInsert(const int tid, const K &key, const V &value) {
    SearchInfo info;
    while (true) {
        auto guard = recmgr->getGuard(tid);

        int res;
        kcas::start();

        while ((res = (search(tid, info, key))) == RetCode::RETRY || IS_MARKED(info.pVer) || IS_MARKED(info.nVer)) {
            kcas::start();
            info.reset();
            GSTATS_ADD(tid, abtree_search_repeat, 1);
            /* keep trying until we get a result */
        }
        GSTATS_ADD(tid, abtree_search_complete, 1);

        if (res == RetCode::SUCCESS) {
            return info.val;
        }

        assert(res == RetCode::FAILURE);
        if (insert(tid, info, key, value)) {
            assert(validate());
            return NO_VALUE;
        }

        GSTATS_ADD(tid, abtree_insert_repeat, 1);
    }
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::insert(const int tid, SearchInfo &info, const K &key, const V &value) {

    auto node = TO_LEAF(info.node);
    auto parent = TO_INTERNAL(info.parent);
    assert(node->leaf);
    assert(!parent->leaf);

    int currSize = node->size;
    if (currSize < b) {
        //we have the capacity to fit this new key
        // find empty slot
        for (int i = 0; i < DEGREE; ++i) {
            if (node->keys[i] == (K)0) {
                Node<K, V, DEGREE> *currValue = node->ptrs[i];
                kcas::add(
                    &node->keys[i], (K)0, key,
                    &node->ptrs[i], currValue, (Node<K, V, DEGREE> *)value,
                    &node->size, currSize, currSize + 1,
                    &node->vNumMark, info.nVer, info.nVer + 2);

                GSTATS_ADD(tid, abtree_update_kcas_attempt, 1);
                if (kcas::validateAndExecute()) {
                    fixDegreeViolation(tid, node);
                    return RetCode::SUCCESS;
                }
                return RetCode::RETRY;
            }
        }
        return RetCode::RETRY;
    }
    else {
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
            left->keys[i].setInitVal(tosort[i].key);
            left->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[i].val);
        }

        const int rightSize = (DEGREE + 1) - leftSize;
        auto right = createExternalNode(tid, true, rightSize, tosort[leftSize].key);
        for (int i = 0; i < rightSize; i++) {
            right->keys[i].setInitVal(tosort[i + leftSize].key);
            right->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[i + leftSize].val);
        }

        auto replacementNode = createInternalNode(tid, parent == entry, 2, tosort[leftSize].key);
        replacementNode->keys[0] = tosort[leftSize].key;
        replacementNode->ptrs[0].setInitVal((Node<K, V, DEGREE> *)left);
        replacementNode->ptrs[1].setInitVal((Node<K, V, DEGREE> *)right);

        // note: weight of new internal node n will be zero,
        //       unless it is the root; this is because we test
        //       p == entry, above; in doing this, we are actually
        //       performing Root-Zero at the same time as this Overflow
        //       if n will become the root
        assert(!IS_MARKED(info.nVer));
        assert(IS_MARKED(info.nVer + 3));
        assert(!IS_MARKED(info.pVer));
        kcas::add(
            &parent->ptrs[info.nodeIndex], (Node<K, V, DEGREE> *)node, (Node<K, V, DEGREE> *)replacementNode,
            &parent->vNumMark, info.pVer, info.pVer + 2,
            &node->vNumMark, info.nVer, info.nVer + 3);
        GSTATS_ADD(tid, abtree_update_kcas_attempt, 1);
        if (kcas::validateAndExecute()) {
            RECLAIM_NODE(node);
            //after overflow, there may be a weight violation at n

            fixWeightViolation(tid, replacementNode);
            return RetCode::SUCCESS;
        }
        DEALLOCATE_NODE(replacementNode);
        DEALLOCATE_NODE(right);
        DEALLOCATE_NODE(left);
        return RetCode::RETRY;
    }
    // }
    assert(false);
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
V ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::tryErase(const int tid, const K &key) {
    SearchInfo info;

    while (true) {
        auto guard = recmgr->getGuard(tid);

        int res;
        kcas::start();

        while ((res = (search(tid, info, key))) == RetCode::RETRY || IS_MARKED(info.pVer) || IS_MARKED(info.nVer)) {
            kcas::start();
            info.reset();
            GSTATS_ADD(tid, abtree_search_repeat, 1);
        }
        GSTATS_ADD(tid, abtree_search_complete, 1);

        if (res == RetCode::FAILURE) {
            if(kcas::validate()) return NO_VALUE;
            continue;
        }

        assert(res == RetCode::SUCCESS);
        if (erase(tid, info, key)) {
            return info.val;
        }

        GSTATS_ADD(tid, abtree_erase_repeat, 1);
    }
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::erase(const int tid, SearchInfo &info, const K &key) {
    auto node = TO_LEAF(info.node);
    auto parent = TO_INTERNAL(info.parent);
    auto gParent = TO_INTERNAL(info.gParent);
    assert(node->leaf);
    assert(!parent->leaf);
    assert(gParent == NULL || !gParent->leaf);

    assert(info.nVer != -1);

    assert(!IS_MARKED(info.nVer));
    int currSize = node->size;
    if (currSize == 0) return RetCode::RETRY; // can only happen if we experience concurrency and are doomed to fail our kcas anyway...
    kcas::add(
        &node->keys[info.keyIndex], key, (K)0,
        &node->size, currSize, currSize - 1,
        &node->vNumMark, info.nVer, info.nVer + 2);
    GSTATS_ADD(tid, abtree_update_kcas_attempt, 1);
    if (kcas::execute()) {
        fixDegreeViolation(tid, node);
        return RetCode::SUCCESS;
    }
    return RetCode::RETRY;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::fixWeightViolation(const int tid, Node<K, V, DEGREE> *viol) {
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

        // do an optimistic check to see if viol was already removed from the tree
        if (IS_MARKED(viol->vNumMark)) {
            // recall that nodes are finalized precisely when
            // they are removed from the tree
            // we hand off responsibility for any violations at viol to the
            // process that removed it.
            return RetCode::UNNECCESSARY;
        }

        GSTATS_ADD(tid, abtree_fix_weight_viol_attempt, 1);

        SearchInfo info;
        kcas::start();

        while (searchTarget(tid, info, viol, viol->searchKey) == RetCode::RETRY || IS_MARKED(info.nVer) || IS_MARKED(info.pVer) || IS_MARKED(info.gVer)) {
            /* keep trying until we get a result */
            info.reset();
            kcas::start();
            GSTATS_ADD(tid, abtree_search_repeat, 1);
        }
        GSTATS_ADD(tid, abtree_search_complete, 1);

        //recall: all these pointers are guaranteed to either be unmarked, or have a different version number that their observedNode's version numbers
        auto nodeBase = info.node;
        auto parent = TO_INTERNAL(info.parent);
        auto gParent = TO_INTERNAL(info.gParent);

        if (nodeBase != viol) {
            // viol was replaced by another update.
            // we hand over responsibility for viol to that update.
            if(kcas::validate()) return RetCode::UNNECCESSARY;
            continue;
        }

        // we cannot apply this update if p has a weight violation
        // so, we check if this is the case, and, if so, try to fix it
        if (!parent->weight) {
            fixWeightViolation(tid, (Node<K, V, DEGREE> *)parent);
            continue;
        }

        auto node = TO_INTERNAL(nodeBase); // note: same as viol
        const int psize = parent->size;
        const int nsize = viol->size;
        const int c = psize + nsize;
        const int size = c - 1;

        if (size <= b) {
            assert(!nodeBase->leaf);
            /**
             * Absorb
             */

             // create new node(s)
             // the new arrays are small enough to fit in a single node,
             // so we replace p by a new internal node.
            auto absorber = createInternalNode(tid, true, size, (K)0x0);
            caswordarraycopy(parent->ptrs, 0, absorber->ptrs, 0, info.nodeIndex);
            caswordarraycopy(node->ptrs, 0, absorber->ptrs, info.nodeIndex, nsize);
            caswordarraycopy(parent->ptrs, info.nodeIndex + 1, absorber->ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));

            arraycopy(parent->keys, 0, absorber->keys, 0, info.nodeIndex);
            arraycopy(node->keys, 0, absorber->keys, info.nodeIndex, getKeyCount((Node<K, V, DEGREE> *)node));
            arraycopy(parent->keys, info.nodeIndex, absorber->keys, info.nodeIndex + getKeyCount((Node<K, V, DEGREE> *)node), getKeyCount((Node<K, V, DEGREE> *)parent) - info.nodeIndex);
            //hate this, speaks to how setInitVal should be renamed to something like setUnsharedVal or something
            absorber->searchKey.setInitVal(absorber->keys[0]); // TODO: verify this is same as in llx/scx abtree


            kcas::add(
                &gParent->ptrs[info.parentIndex], (Node<K, V, DEGREE> *)parent, (Node<K, V, DEGREE> *)absorber,
                &gParent->vNumMark, info.gVer,  info.gVer + 2,
                &parent->vNumMark, info.pVer, info.pVer + 3,
                &node->vNumMark, info.nVer, info.nVer + 3);

            GSTATS_ADD(tid, abtree_rebalancing_kcas_attempt, 1);
            if (kcas::execute()) {
                RECLAIM_NODE(node);
                RECLAIM_NODE(parent);

                fixDegreeViolation(tid, (Node<K, V, DEGREE> *)absorber);

                return RetCode::SUCCESS;
            }

            DEALLOCATE_NODE(absorber);
        }
        else {
            assert(!nodeBase->leaf);
            auto node = TO_INTERNAL(nodeBase);
            /**
             * Split
             */

             // merge keys of p and l into one big array (and similarly for children)
             // (we essentially replace the pointer to l with the contents of l)
            K keys[2 * DEGREE];
            Node<K, V, DEGREE> * ptrs[2 * DEGREE];
            arraycopy(parent->ptrs, 0, ptrs, 0, info.nodeIndex);
            arraycopy(node->ptrs, 0, ptrs, info.nodeIndex, nsize);
            arraycopy(parent->ptrs, info.nodeIndex + 1, ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));
            arraycopy(parent->keys, 0, keys, 0, info.nodeIndex);
            arraycopy(node->keys, 0, keys, info.nodeIndex, getKeyCount((Node<K, V, DEGREE> *)node));
            arraycopy(parent->keys, info.nodeIndex, keys, info.nodeIndex + getKeyCount((Node<K, V, DEGREE> *)node), getKeyCount((Node<K, V, DEGREE> *)parent) - info.nodeIndex);

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
            caswordarraycopy(ptrs, 0, left->ptrs, 0, leftSize);

            const int rightSize = size - leftSize;
            auto right = createInternalNode(tid, true, rightSize, keys[leftSize]);
            arraycopy(keys, leftSize, right->keys, 0, rightSize - 1);
            caswordarraycopy(ptrs, leftSize, right->ptrs, 0, rightSize);

            // note: keys[leftSize - 1] should be the same as n->keys[0]
            auto newNode = createInternalNode(tid, gParent == entry, 2, keys[leftSize - 1]);
            newNode->keys[0] = keys[leftSize - 1];
            newNode->ptrs[0].setInitVal((Node<K, V, DEGREE> *)left);
            newNode->ptrs[1].setInitVal((Node<K, V, DEGREE> *)right);

            // note: weight of new internal node n will be zero,
            //       unless it is the root; this is because we test
            //       gp == entry, above; in doing this, we are actually
            //       performing Root-Zero at the same time as this Overflow
            //       if n will become the root

            kcas::add(
                &gParent->ptrs[info.parentIndex], (Node<K, V, DEGREE> *)parent, (Node<K, V, DEGREE> *)newNode,
                &gParent->vNumMark, info.gVer, info.gVer + 2,
                &parent->vNumMark, info.pVer, info.pVer + 3,
                &node->vNumMark, info.nVer, info.nVer + 3);

            GSTATS_ADD(tid, abtree_rebalancing_kcas_attempt, 1);
            if (kcas::execute()) {
                RECLAIM_NODE(node);
                RECLAIM_NODE(parent);

                fixWeightViolation(tid, (Node<K, V, DEGREE> *)newNode);
                fixDegreeViolation(tid, (Node<K, V, DEGREE> *)newNode);

                return RetCode::SUCCESS;
            }

            DEALLOCATE_NODE(left);
            DEALLOCATE_NODE(right);
            DEALLOCATE_NODE(newNode);
        }
    }
    assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
int ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::fixDegreeViolation(const int tid, Node<K, V, DEGREE> *viol) {
    // we search for viol and try to fix any violation we find there
    // this entails performing AbsorbSibling or Distribute.
    NodeInternal<K, V, DEGREE> *parent;
    NodeInternal<K, V, DEGREE> *gParent;
    Node<K, V, DEGREE> *node = NULL;
    Node<K, V, DEGREE> *sibling = NULL;
    Node<K, V, DEGREE> *left = NULL;
    Node<K, V, DEGREE> *right = NULL;

    while (true) {
        //see the monster comment in fixWeightViolation as to why these checks happen in the loop now
        if (viol->size >= a || viol == (Node<K, V, DEGREE> *)entry || viol == entry->ptrs[0]) {
            return RetCode::UNNECCESSARY; // no degree violation at viol
        }

        // do an optimistic check to see if viol was already removed from the tree
        if (IS_MARKED(viol->vNumMark)) {
            // recall that nodes are finalized precisely when
            // they are removed from the tree.
            // we hand off responsibility for any violations at viol to the
            // process that removed it.
            return RetCode::UNNECCESSARY;
        }

        GSTATS_ADD(tid, abtree_fix_degree_viol_attempt, 1);

        /**
         * search for viol
         */
        SearchInfo info;
        kcas::start();
        while (searchTarget(tid, info, viol, viol->searchKey) == RetCode::RETRY || IS_MARKED(info.nVer) || IS_MARKED(info.pVer) || IS_MARKED(info.gVer)) {
            /* keep trying until we get a result */
            info.reset();
            kcas::start();
            GSTATS_ADD(tid, abtree_search_repeat, 1);
        }
        GSTATS_ADD(tid, abtree_search_complete, 1);
        //recall: all these pointers are guaranteed to either be unmarked, or have a different version number than their observedNode's version numbers

        NodeExternal<K, V, DEGREE> *nodeExt = NULL;
        NodeInternal<K, V, DEGREE> *nodeInt = NULL;

        node = info.node;

        if (node != viol) {
            // viol was replaced by another update.
            // we hand over responsibility for viol to that update.
            if(kcas::validate()) return RetCode::UNNECCESSARY;
            continue;
        }

        if (node->leaf) {
            nodeExt = TO_LEAF(node);
        }
        else {
            nodeInt = TO_INTERNAL(node);
        }

        parent = TO_INTERNAL(info.parent);
        gParent = TO_INTERNAL(info.gParent);



        // assert: gp != NULL (because if AbsorbSibling or Distribute can be applied, then p is not the root)
        int siblingIndex = (info.nodeIndex > 0 ? info.nodeIndex - 1 : 1);
        sibling = parent->ptrs[siblingIndex];

        //sibling is not part of the path, so we need to make sure it's not marked and store its vNum before reading any fields
        casword_t sVer = kcas::visit(sibling);

        if (IS_MARKED(sVer)) {
            continue;
        }

        // we can only apply AbsorbSibling or Distribute if there are no
        // weight violations at p, l or s.
        // so, we first check for any weight violations,
        // and fix any that we see.
        bool foundWeightViolation = false;
        if (!parent->weight) {
            foundWeightViolation = true;
            fixWeightViolation(tid, (Node<K, V, DEGREE> *)parent);
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
        Node<K, V, DEGREE> *left = NULL;
        Node<K, V, DEGREE> *right = NULL;

        if (info.nodeIndex < siblingIndex) {
            left = node;
            right = sibling;
            leftIndex = info.nodeIndex;
            rightIndex = siblingIndex;
        }
        else {
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

        // crucially, unlike in the llx/scx version, the keys can change beyond this point,
        // *IFF* we are dealing with external nodes (leaves).
        // so, in that case, we have to watch out for overflowing the tosort array...

        if (size < 2 * a) {
            /**
             * AbsorbSibling
             */

             // assert(left->weight && right->weight && parent->weight); // or ver# has changed
            Node<K, V, DEGREE> *newNode = NULL;
            // create new node(s))

            int keyCounter = 0, ptrCounter = 0;

            if (left->leaf) {
                auto leftExt = TO_LEAF(left);

                //duplicate code can be cleaned up, but it would make it far less readable...
                auto newNodeExt = createExternalNode(tid, true, size, node->searchKey);
                for (int i = 0; i < DEGREE; i++) {
                    if (leftExt->keys[i]) {
                        newNodeExt->keys[keyCounter++].setInitVal(leftExt->keys[i]);
                        newNodeExt->ptrs[ptrCounter++].setInitVal(leftExt->ptrs[i]);
                    }
                }

                assert(right->leaf);
                // if (right->leaf) {
                bool restart = false;
                auto rightExt = TO_LEAF(right);
                for (int i = 0; i < DEGREE; i++) {
                    if (rightExt->keys[i]) {
                        // BEWARE: MUST NOT OVERFLOW SINGLE NODE SIZE HERE.
                        if (keyCounter >= DEGREE) {
                            recmgr->deallocate(tid, newNodeExt);
                            restart = true;
                            break;
                        }
                        newNodeExt->keys[keyCounter++].setInitVal(rightExt->keys[i]);
                        newNodeExt->ptrs[ptrCounter++].setInitVal(rightExt->ptrs[i]);
                    }
                }
                if (restart) continue;


                newNode = (Node<K, V, DEGREE> *)newNodeExt;
            }
            else {

                auto leftInt = TO_INTERNAL(left);

                auto newNodeInt = createInternalNode(tid, true, size, node->searchKey);

                for (int i = 0; i < getKeyCount(left); i++) {
                    newNodeInt->keys[keyCounter++] = leftInt->keys[i];
                }

                newNodeInt->keys[keyCounter++] = parent->keys[leftIndex];

                for (int i = 0; i < lsize; i++) {
                    newNodeInt->ptrs[ptrCounter++].setInitVal(leftInt->ptrs[i]);
                }

                assert(!right->leaf);

                auto rightInt = TO_INTERNAL(right);

                for (int i = 0; i < getKeyCount(right); i++) {
                    newNodeInt->keys[keyCounter++] = rightInt->keys[i];
                }

                for (int i = 0; i < rsize; i++) {
                    newNodeInt->ptrs[ptrCounter++].setInitVal(rightInt->ptrs[i]);
                }


                newNode = (Node<K, V, DEGREE> *)newNodeInt;
            }

            // now, we atomically replace p and its children with the new nodes.
            // if appropriate, we perform RootAbsorb at the same time.
            if (gParent == entry && psize == 2) {
                // some quick checks to avoid calling kcas if we are doomed...
                if (gParent->vNumMark != info.gVer) goto cleanup_retry0;
                if (parent->vNumMark != info.pVer) goto cleanup_retry0;
                if (node->vNumMark != info.nVer) goto cleanup_retry0;
                if (sibling->vNumMark != sVer) goto cleanup_retry0;

                kcas::add(
                    &gParent->ptrs[info.parentIndex], (Node<K, V, DEGREE> *)parent, newNode,
                    &gParent->vNumMark, info.gVer, info.gVer + 2,
                    &parent->vNumMark, info.pVer, info.pVer + 3,
                    &node->vNumMark, info.nVer, info.nVer + 3,
                    &sibling->vNumMark, sVer, sVer+ 3);

                GSTATS_ADD(tid, abtree_rebalancing_kcas_attempt, 1);
                if (kcas::execute()) {
                    RECLAIM_NODE(node);
                    RECLAIM_NODE(parent);
                    RECLAIM_NODE(sibling);

                    fixDegreeViolation(tid, newNode);
                    return RetCode::SUCCESS;
                }
                cleanup_retry0:
                DEALLOCATE_NODE(newNode);
            }
            else {

                assert(gParent != entry || psize > 2);

                // create n from p by:
                // 1. skipping the key for leftindex and child pointer for ixToS
                // 2. replacing l with newl
                auto newParent = createInternalNode(tid, true, psize - 1, parent->searchKey);
                for (int i = 0; i < leftIndex; i++) {
                    newParent->keys[i] = parent->keys[i];
                }
                for (int i = 0; i < siblingIndex; i++) {
                    newParent->ptrs[i].setInitVal(parent->ptrs[i]);
                }
                for (int i = leftIndex + 1; i < getKeyCount((Node<K, V, DEGREE> *)parent); i++) {
                    newParent->keys[i - 1] = parent->keys[i];
                }
                for (int i = info.nodeIndex + 1; i < psize; i++) {
                    newParent->ptrs[i - 1].setInitVal(parent->ptrs[i]);
                }

                // replace l with newl in n's pointers
                newParent->ptrs[info.nodeIndex - (info.nodeIndex > siblingIndex)].setInitVal(newNode);

                // some quick checks to avoid calling kcas if we are doomed...
                if (gParent->vNumMark != info.gVer) goto cleanup_retry1;
                if (parent->vNumMark != info.pVer) goto cleanup_retry1;
                if (node->vNumMark != info.nVer) goto cleanup_retry1;
                if (sibling->vNumMark != sVer) goto cleanup_retry1;

                kcas::add(
                    &gParent->ptrs[info.parentIndex], (Node<K, V, DEGREE> *)parent, (Node<K, V, DEGREE> *)newParent,
                    &gParent->vNumMark, info.gVer, info.gVer + 2,
                    &parent->vNumMark, info.pVer, info.pVer + 3,
                    &node->vNumMark, info.nVer, info.nVer + 3,
                    &sibling->vNumMark, sVer, sVer + 3);

                GSTATS_ADD(tid, abtree_rebalancing_kcas_attempt, 1);
                if (kcas::execute()) {
                    RECLAIM_NODE(node);
                    RECLAIM_NODE(parent);
                    RECLAIM_NODE(sibling);

                    fixDegreeViolation(tid, newNode);
                    fixDegreeViolation(tid, (Node<K, V, DEGREE> *)newParent);

                    return RetCode::SUCCESS;
                }
                cleanup_retry1:
                DEALLOCATE_NODE(newParent);
                DEALLOCATE_NODE(newNode);
            }

        }
        else {
            /**
             * Distribute
             */

            int leftSize = size / 2;
            int rightSize = size - leftSize;

            Node<K, V, DEGREE> *newLeft = NULL;
            Node<K, V, DEGREE> *newRight = NULL;

            kvpair<K> tosort[DEGREE + 1];

            // combine the contents of l and s (and one key from p if l and s are internal)

            int keyCounter = 0;
            int valCounter = 0;
            if (left->leaf) {
                assert(right->leaf);
                for (int i = 0; i < DEGREE; i++) {
                    if (TO_LEAF(left)->keys[i]) {
                        tosort[keyCounter++].key = TO_LEAF(left)->keys[i];
                        tosort[valCounter++].val = TO_LEAF(left)->ptrs[i];
                    }
                }
            }
            else {
                for (int i = 0; i < getKeyCount(left); i++) {
                    tosort[keyCounter++].key = TO_INTERNAL(left)->keys[i];
                }

                for (int i = 0; i < lsize; i++) {
                    tosort[valCounter++].val = TO_INTERNAL(left)->ptrs[i];
                }
            }

            if (!left->leaf)
                tosort[keyCounter++].key = parent->keys[leftIndex];

            if (right->leaf) {
                bool restart = false;
                for (int i = 0; i < DEGREE; i++) {
                    if (TO_LEAF(right)->keys[i]) {
                        // BEWARE: MUST NOT OVERFLOW tosort ARRAY HERE.
                        if (keyCounter >= 1+DEGREE) {
                            restart = true;
                            break;
                        }
                        tosort[keyCounter++].key = TO_LEAF(right)->keys[i];
                        tosort[valCounter++].val = TO_LEAF(right)->ptrs[i];
                    }
                }
                if (restart) continue;
            }
            else {
                for (int i = 0; i < getKeyCount(right); i++) {
                    tosort[keyCounter++].key = TO_INTERNAL(right)->keys[i];
                }

                for (int i = 0; i < rsize; i++) {
                    tosort[valCounter++].val = TO_INTERNAL(right)->ptrs[i];
                }
            }

            if (left->leaf)
                qsort(tosort, keyCounter, sizeof(kvpair<K>), kv_compare<K, Compare>);

            keyCounter = 0;
            valCounter = 0;
            K pivot;

            if (left->leaf) {
                NodeExternal<K, V, DEGREE> *newLeftExt = createExternalNode(tid, true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize; i++) {
                    newLeftExt->keys[i].setInitVal(tosort[keyCounter++].key);
                    newLeftExt->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[valCounter++].val);
                }

                newLeft = (Node<K, V, DEGREE> *)newLeftExt;
                newLeft->searchKey.setInitVal(newLeftExt->keys[0]);
                pivot = tosort[keyCounter].key;

            }
            else {
                NodeInternal<K, V, DEGREE> *newLeftInt = createInternalNode(tid, true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize - 1; i++) {
                    newLeftInt->keys[i] = tosort[keyCounter++].key;
                }

                for (int i = 0; i < leftSize; i++) {
                    newLeftInt->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[valCounter++].val);
                }

                newLeft = (Node<K, V, DEGREE> *)newLeftInt;
                newLeft->searchKey.setInitVal(newLeftInt->keys[0]);
                pivot = tosort[keyCounter++].key;
            }

            // reserve one key for the parent (to go between newleft and newright))

            if (right->leaf) {
                assert(left->leaf);
                auto newRightExt = createExternalNode(tid, true, rightSize, (K)0x0);
                bool restart = false;
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    // BEWARE: MUST NOT OVERFLOW tosort ARRAY HERE.
                    if (keyCounter >= 1+DEGREE) {
                        assert(newLeft);
                        recmgr->deallocate(tid, (NodeExternal<K, V, DEGREE> *) newLeft);
                        recmgr->deallocate(tid, newRightExt);
                        restart = true;
                        break;
                    }
                    newRightExt->keys[i].setInitVal(tosort[keyCounter++].key);
                }
                if (restart) continue;
                newRight = (Node<K, V, DEGREE> *)newRightExt;
                newRight->searchKey.setInitVal(newRightExt->keys[0]); // TODO: verify searchKey setting is same as llx/scx based version

                for (int i = 0; i < rightSize; i++) {
                    TO_LEAF(newRight)->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[valCounter++].val);
                }
            }
            else {
                NodeInternal<K, V, DEGREE> *newRightInt = createInternalNode(tid, true, rightSize, (K)0x0);
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    newRightInt->keys[i] = tosort[keyCounter++].key;
                }
                newRight = (Node<K, V, DEGREE> *)newRightInt;
                newRight->searchKey.setInitVal(newRightInt->keys[0]);

                for (int i = 0; i < rightSize; i++) {
                    TO_INTERNAL(newRight)->ptrs[i].setInitVal((Node<K, V, DEGREE> *)tosort[valCounter++].val);
                }
            }

            //because you make keys not caswords in internal nodes, and you want to change one,
            //in this case you need to replace the parent, despite not having to before
            auto newParent = createInternalNode(tid, parent->weight, psize, parent->searchKey);
            arraycopy(parent->keys, 0, newParent->keys, 0, getKeyCount((Node<K, V, DEGREE> *)parent));
            caswordarraycopy(parent->ptrs, 0, newParent->ptrs, 0, psize);
            newParent->ptrs[leftIndex].setInitVal(newLeft);
            newParent->ptrs[rightIndex].setInitVal(newRight);
            newParent->keys[leftIndex] = pivot;

            // some quick checks to avoid calling kcas if we are doomed...
            if (gParent->vNumMark != info.gVer) goto cleanup_retry2;
            if (parent->vNumMark != info.pVer) goto cleanup_retry2;
            if (node->vNumMark != info.nVer) goto cleanup_retry2;
            if (sibling->vNumMark != sVer) goto cleanup_retry2;

            kcas::add(
                &gParent->ptrs[info.parentIndex], (Node<K, V, DEGREE> *)parent, (Node<K, V, DEGREE> *)newParent,
                &gParent->vNumMark, info.gVer, info.gVer + 2,
                &parent->vNumMark, info.pVer, info.pVer + 3,
                &node->vNumMark, info.nVer, info.nVer + 3,
                &sibling->vNumMark, sVer, sVer + 3);

            GSTATS_ADD(tid, abtree_rebalancing_kcas_attempt, 1);
            if (kcas::execute()) {
                RECLAIM_NODE(node);
                RECLAIM_NODE(sibling);
                RECLAIM_NODE(parent);

                fixDegreeViolation(tid, newParent);

                return RetCode::SUCCESS;
            }
            cleanup_retry2:
            DEALLOCATE_NODE(newLeft);
            DEALLOCATE_NODE(newRight);
            DEALLOCATE_NODE(newParent);
        }
    }
    assert(false);
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
void ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::printDebuggingDetails() {
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
long ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::validateSubtree(Node<K, V, DEGREE> *node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound) {

    if (node == NULL) return 0;
    // graph << "\"" << node << "\"" << "[label=\"K: " << node->searchKey << " - W: "
    //         << node->weight << " - L: " << node->leaf << " - N: " << node << "\"];\n";
    graph << "\"" << node << "\"" << "[shape=record, label=\"S" << node->searchKey << " | W"
            << node->weight << " | L" << node->leaf; //<< "\"];\n";
    int last = -1;
    if (node->leaf) {
        auto leaf = TO_LEAF(node);
        for (int i = 0; i < DEGREE; i++) {
            K key = leaf->keys[i].getValue();
            graph << " | <k"<<i<<">";
            if (key) graph << key; else graph << "x";
        }
    } else {
        auto internal = TO_INTERNAL(node);
        for (int i = 0; i < internal->size-1; i++) {
            K key = internal->keys[i];
            graph << " | <p"<<i<<">";
            graph << " | <k"<<i<<">";
            if (key) graph << key; else graph << "x";
        }
        graph << " | <p"<<(internal->size-1)<<">";
    }
    graph << " \"];\n";

    if (IS_MARKED(node->vNumMark)) {
        log << "MARKED NODE! " << node->searchKey << "\n";
        errorFound = true;
    }

    if (!node->weight) {
        log << "Weight Violation! " << node->searchKey << "\n";
        errorFound = true;
    }

    if (node->leaf) {
        for (int i = 0; i < DEGREE; i++) {
            auto leaf = TO_LEAF(node);
            K key = leaf->keys[i].getValue();
            if (key) {
                // graph << "\"" << node << "\" -> \"" << key << "\";\n";
                if (key < 0 || key > MAXKEY) {
                    log << "Suspected pointer in leaf! " << node->searchKey << "\n";
                    errorFound = true;
                }
                if (keys.count(key) > 0) {
                    log << "DUPLICATE KEY! " << node->searchKey << "\n";
                    errorFound = true;
                }
                keys.insert(key);
            }
        }
    }

    if (!node->leaf) {
        for (int i = 0; i < node->size; i++) {
            graph << "\"" << node << "\":<p"<<i<<"> -> \"" << TO_INTERNAL(node)->ptrs[i] << "\";\n";
            validateSubtree(TO_INTERNAL(node)->ptrs[i], keys, graph, log, errorFound);
        }
    }
    return 1;
}

template <class RecordManager, typename K, typename V, int DEGREE, class Compare>
bool ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::validate() {
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

    // long ret = validateSubtree(entry, keys, graph, log, errorFound);
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
void ABTreeKCAS<RecordManager, K, V, DEGREE, Compare>::freeSubtree(const int tid, Node<K, V, DEGREE> *node) {
    if (!node->leaf) {
        for (int i = 0; i < node->size; i++) {
            freeSubtree(tid, TO_INTERNAL(node)->ptrs[i]);
        }
    }
    DEALLOCATE_NODE(node);
}
