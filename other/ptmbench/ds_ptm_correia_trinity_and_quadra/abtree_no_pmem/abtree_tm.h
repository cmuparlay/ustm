#pragma once

#include <cassert>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#define TMTYPE trinityvrtl2::persist

using namespace std;

#define PADDING_BYTES 128


#if defined(ESLOCO_ENABLE_DEFERRED_FREE) && !defined(DISABLE_TM_ALLOCATOR)
    #define RECLAIM_NODE(node) trinityvrtl2::Trinity::tmDeferredDelete<Node<K,V,DEGREE>>(node);
#elif !defined(DISABLE_TM_ALLOCATOR)
    #define RECLAIM_NODE(node) trinityvrtl2::Trinity::tmDelete<Node<K,V,DEGREE>>(node);
#else 
    #define RECLAIM_NODE(tid, node) 
#endif

// #define RECLAIM_NODE(node) 
// #define DEALLOCATE_NODE(node) 

#define tmarraycopy(src, srcStart, dest, destStart, len)                 \
    for (int ___i = 0; ___i < (len); ++___i) {                           \
        (dest)[(destStart) + ___i] = (src)[(srcStart) + ___i].pload(); \
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

template <typename K>
int kv_compare(const void *_a, const void *_b) {
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

template <typename K, typename V, int DEGREE>
class Node {
  public:
    TMTYPE<int> size;         // DEGREE of node
    TMTYPE<K> keys[DEGREE];
    TMTYPE<Node<K,V,DEGREE>*> ptrs[DEGREE]; // also doubles as a spot for VALUES
    TMTYPE<K> searchKey;    // key that can be used to find this node (even if its empty) // const
    TMTYPE<int> weight;     // 0 or 1 // const
    TMTYPE<bool> leaf;      // 0 or 1 // const
    // K searchKey;    // key that can be used to find this node (even if its empty) // const
    // int weight;     // 0 or 1 // const
    // bool leaf;      // 0 or 1 // const

    // TMTYPE<int> size;         // DEGREE of node
    // TMTYPE<K> keys[DEGREE];
    // TMTYPE<Node<K,V,DEGREE>*> ptrs[DEGREE]; // also doubles as a spot for VALUES
    Node() {
    }
};
#define nodeptr Node<K,V,DEGREE> *

enum RetCode : int {
    RETRY = 0,
    UNNECCESSARY = 0,
    FAILURE = -1,
    SUCCESS = 1,
};

template <typename K, typename V, int DEGREE>
class ABTreeTM {
  private:
    // volatile char padding2[PADDING_BYTES];
    // nodeptr entry;
    TMTYPE<nodeptr> entry;
    volatile char padding1[PADDING_BYTES];
    const int a;
    // TMTYPE<int> a;
    const int b;
    // TMTYPE<int> b;
    K maxKey;
    // TMTYPE<K> maxKey;    
    volatile char padding5[PADDING_BYTES];

  public:
    volatile char padding0[PADDING_BYTES];
    // void * const NO_VALUE;
    const V NO_VALUE {0};

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

    

  public:
    ABTreeTM(const K anyKey, const K _maxKey);
    ~ABTreeTM();
    bool contains(const int tid, const K &key);
    V tryInsert(const int tid, const K &key, const V &value);
    V tryErase(const int tid, const K &key);
    V find(const int tid, const K &key);
    void printDebuggingDetails();
    void initThread(const int tid);
    void deinitThread(const int tid);
    bool validate();
    long validateSubtree(nodeptr node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound);
#ifdef PHTYTM_ENABLE_RANGE_QUERY  
    int rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, void ** const resultValues);
#endif

  private:
    int getKeyCount(nodeptr node);
    int getChildIndex(nodeptr node, const K &key);
    int getKeyIndex(nodeptr node, const K &key);
    nodeptr createInternalNode(const int tid, bool weight, int size, K searchKey);
    nodeptr createExternalNode(const int tid, bool weight, int size, K searchKey);
    int erase(const int tid, SearchInfo &info, const K &key);
    int insert(const int tid, SearchInfo &info, const K &key, const V &value);
    V searchBasic(const int tid, const K &key);
    int search(const int tid, SearchInfo &info, const K &key, nodeptr target = NULL);
    int searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key);
    int fixDegreeViolation(const int tid, nodeptr viol);
    int fixWeightViolation(const int tid, nodeptr viol);
#ifdef PHTYTM_ENABLE_RANGE_QUERY
    void traversal_try_add(const int tid, nodeptr const node, K * const rqResultKeys, V * const rqResultValues, int * const startIndex, const K& lo, const K& hi);
    int getKeys(const int tid, nodeptr node, K * const outputKeys, void ** const outputValues);
    bool isInRange(const K& key, const K& lo, const K& hi);
#endif
};

template <typename K, typename V, int DEGREE>
inline int ABTreeTM<K, V, DEGREE>::getKeyCount(nodeptr node) {
    int sz = node->size;
    if (!node->leaf) {
        sz -= 1;
    }
    return sz;
    // return node->leaf ? node->size : node->size - 1;
}

template <typename K, typename V, int DEGREE>
inline int ABTreeTM<K, V, DEGREE>::getChildIndex(nodeptr node, const K &key) {
    int nkeys = getKeyCount(node);
    int retval = 0;
    K nodeKey = node->keys[retval];
    // while (retval < nkeys && !compare(key, (const K &)node->keys[retval])) {
    while (retval < nkeys && key >= nodeKey) {
        ++retval;
        nodeKey = node->keys[retval];
    }
    return retval;
}

template <typename K, typename V, int DEGREE>
inline int ABTreeTM<K, V, DEGREE>::getKeyIndex(nodeptr node, const K &key) {
    int retval = 0;
    while (retval < DEGREE && node->keys[retval] != key) {
        ++retval;
    }
    return retval;
}

template <typename K, typename V, int DEGREE>
nodeptr ABTreeTM<K, V, DEGREE>::createInternalNode(const int tid, bool weight, int size, K searchKey) {
    nodeptr node = trinityvrtl2::Trinity::tmNew<Node<K,V,DEGREE>>();    
    node->leaf = 0;
    node->weight = weight;
    node->size = size;
    node->searchKey = searchKey;
    for (int i=0;i<DEGREE;++i) node->keys[i] = (K) 0;
    // node->leaf.pstore_unsafe(0);
    // node->weight.pstore_unsafe(weight);
    // node->size.pstore_unsafe(size);
    // node->searchKey.pstore_unsafe(searchKey);
    // for (int i=0;i<DEGREE;++i) node->keys[i].pstore_unsafe((K) 0);
    return node;
}

template <typename K, typename V, int DEGREE>
nodeptr ABTreeTM<K, V, DEGREE>::createExternalNode(const int tid, bool weight, int size, K searchKey) {
    nodeptr node = createInternalNode(tid, weight, size, searchKey);
    node->leaf = 1;
    // node->leaf.pstore_unsafe(1);
    return node;
}

// template <typename K, typename V, int DEGREE>
// ABTreeTM<K, V, DEGREE>::ABTreeTM(const K anyKey, const K _maxKey) : 
// b(DEGREE), a(max(DEGREE / 4, 2)),
// NO_VALUE((void *)(uintptr_t)0), maxKey(_maxKey) {
//     assert(sizeof(V) == sizeof(nodeptr));
//     assert(SUCCESS == RetCode::SUCCESS);
//     assert(RETRY == RetCode::RETRY);
//     compare = Compare();
//     const int tid = 0;
//     initThread(tid);
//     // initial tree: entry is a sentinel node (with one pointer and no keys)
//     //               that points to an empty node (no pointers and no keys)
//     nodeptr _entryLeft = createExternalNode(tid, true, 0, anyKey);
//     //sentinel node
//     nodeptr _entry = createInternalNode(tid, true, 1, anyKey);
//     _entry->ptrs[0] = _entryLeft;
//     entry = _entry;
// }

template <typename K, typename V, int DEGREE>
ABTreeTM<K, V, DEGREE>::ABTreeTM(const K anyKey, const K _maxKey) :
    b(DEGREE),
    a(max(DEGREE / 4, 2)),
    maxKey(_maxKey)
{
    assert(sizeof(V) == sizeof(nodeptr));
    assert(SUCCESS == RetCode::SUCCESS);
    assert(RETRY == RetCode::RETRY);

    // b = (DEGREE);
    // a = (max(DEGREE / 4, 2));
    // maxKey = (_maxKey);

    // initial tree: entry is a sentinel node (with one pointer and no keys)
    //               that points to an empty node (no pointers and no keys)
    nodeptr _entryLeft = createExternalNode(tid, true, 0, anyKey);

    //sentinel node
    nodeptr _entry = createInternalNode(tid, true, 1, anyKey);
    _entry->ptrs[0] = _entryLeft;

    entry = _entry;
}

template <typename K, typename V, int DEGREE>
ABTreeTM<K, V, DEGREE>::~ABTreeTM() {
}

template <typename K, typename V, int DEGREE>
void ABTreeTM<K, V, DEGREE>::initThread(const int tid) {
}

template <typename K, typename V, int DEGREE>
void ABTreeTM<K, V, DEGREE>::deinitThread(const int tid) {
}

template <typename K, typename V, int DEGREE>
inline bool ABTreeTM<K, V, DEGREE>::contains(const int tid, const K &key) {   
    bool retval;
    trinityvrtl2::Trinity::readTx<bool>([&] () {
        retval = searchBasic(tid, key) != NO_VALUE;
    });
    return retval;
}

template <typename K, typename V, int DEGREE>
V ABTreeTM<K, V, DEGREE>::find(const int tid, const K &key) {
    V retval;
    trinityvrtl2::Trinity::readTx<bool>([&] () {
        retval = searchBasic(tid, key);
    });
    return retval;
}

/* searchBasic(const int tid, const K &key)
 * Basic search, returns respective value associated with key, or NO_VALUE if nothing is found
 * does not return any path information like other searches (and is therefore slightly faster)
 * called by contains() and find()
 */
template <typename K, typename V, int DEGREE>
V ABTreeTM<K, V, DEGREE>::searchBasic(const int tid, const K &key) {
    while (true) {
        nodeptr node = entry->ptrs[0];
        while (!node->leaf) {
            node = node->ptrs[getChildIndex(node, key)];
            // if (!node) {
            //     validate();
            //     assert(false);
            // }
        }
        int keyIndex = getKeyIndex(node, key);
        if (keyIndex < DEGREE) return (V) node->ptrs[keyIndex];
        else return NO_VALUE;
    }
}

/* search(const int tid, SearchInfo &info, const K &key)
 * normal search used to search for a specific key, fills a SearchInfo struct so the caller
 * can manipulate the nodes around the searched for key
 */
template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::search(const int tid, SearchInfo &info, const K &key, nodeptr target) {
    info.oGParent = NULL;
    info.oParent = entry;
    info.nodeIndex = 0;
    info.oNode = entry->ptrs[0];    
    // int x = 0;    
    // int nodeIndexes[100];
    while (!info.oNode->leaf && (target ? info.oNode != target : true)) {            
        info.oGParent = info.oParent;
        info.oParent = info.oNode;
        info.parentIndex = info.nodeIndex;
        info.nodeIndex = getChildIndex(info.oNode, key);
        // nodeIndexes[x] = info.nodeIndex;
        // if(!info.oNode->ptrs[info.nodeIndex]) {
        //     bool val = validate();
        //     assert(false);
        // }
        info.oNode = info.oNode->ptrs[info.nodeIndex];        
        // x++;        
    }
    if (info.oNode) {
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
    } else {
        return RetCode::FAILURE;
    }

}

/* searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key)
 * Searches for a key, however halts when a specific target node is reached. Return
 * is dependent on if the node halted at is the target (indicating the key searched for leads, at some point,
 * to this node)
 */
template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::searchTarget(const int tid, SearchInfo &info, nodeptr target, const K &key) {
    return search(tid, info, key, target);
}

template <typename K, typename V, int DEGREE>
V ABTreeTM<K, V, DEGREE>::tryInsert(const int tid, const K &key, const V &value) {
    V retval;
    SearchInfo info;
    trinityvrtl2::Trinity::updateTx<bool>([&] () {        
        // printf("thread %d attempting insert key %lld\n", tid, key);   
        while (true) {
            int res = search(tid, info, key, NULL);
            if (res == RetCode::SUCCESS) {            
                retval = info.val;
                break;
            }
            if (insert(tid, info, key, value)) {                        
                retval = NO_VALUE;
                break;
            }        
        }
    });
    return retval;
}

template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::insert(const int tid, SearchInfo &info, const K &key, const V &value) {
    nodeptr node = info.oNode;
    nodeptr parent = info.oParent;
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
                int oldSize = node->size;
                node->size = 1+oldSize;
                fixDegreeViolation(tid, node);
                // assert(validate());
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
        qsort(tosort, k, sizeof(kvpair<K>), kv_compare<K>);

        // create new node(s):
        // since the new arrays are too big to fit in a single node,
        // we replace l by a new subtree containing three new nodes:
        // a parent, and two leaves;
        // the array contents are then split between the two new leaves

        const int leftSize = k / 2;
        nodeptr left = createExternalNode(tid, true, leftSize, tosort[0].key);
        for (int i = 0; i < leftSize; i++) {
            left->keys[i] = tosort[i].key;
            left->ptrs[i] = (nodeptr)tosort[i].val;
        }

        const int rightSize = (DEGREE + 1) - leftSize;
        nodeptr right = createExternalNode(tid, true, rightSize, tosort[leftSize].key);
        for (int i = 0; i < rightSize; i++) {
            right->keys[i] = tosort[i + leftSize].key;
            right->ptrs[i] = (nodeptr)tosort[i + leftSize].val;
        }

        nodeptr replacementNode = createInternalNode(tid, parent == entry, 2, tosort[leftSize].key);
        replacementNode->keys[0] = tosort[leftSize].key;
        replacementNode->ptrs[0] = left;
        replacementNode->ptrs[1] = right;

        // note: weight of new internal node n will be zero,
        //       unless it is the root; this is because we test
        //       p == entry, above; in doing this, we are actually
        //       performing Root-Zero at the same time as this Overflow
        //       if n will become the root

        parent->ptrs[info.nodeIndex] = replacementNode;
        RECLAIM_NODE(node);
        fixWeightViolation(tid, replacementNode);
        // bool val = validate();
        // assert(val);
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

template <typename K, typename V, int DEGREE>
V ABTreeTM<K, V, DEGREE>::tryErase(const int tid, const K &key) {
    V retval;
    SearchInfo info; 
    trinityvrtl2::Trinity::updateTx<bool>([&] () {                 
        // printf("thread %d attempting erase key %lld\n", tid, key);     
        while (true) {     
            int res = search(tid, info, key);
            if (res == RetCode::FAILURE) {                        
                retval = NO_VALUE;
                break;
            }
            if (erase(tid, info, key)) {            
                retval = info.val;
                break;
            }
        }
    });
    return retval;
}

template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::erase(const int tid, SearchInfo &info, const K &key) {
    nodeptr node = info.oNode;
    nodeptr parent = info.oParent;
    nodeptr gParent = info.oGParent;
    assert(node->leaf);
    assert(!parent->leaf);
    assert(gParent == NULL || !gParent->leaf);
    int currSize = node->size;
    node->keys[info.keyIndex] = (K) 0;
    node->size = currSize-1;
    fixDegreeViolation(tid, node);
    return RetCode::SUCCESS;
}

template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::fixWeightViolation(const int tid, nodeptr viol) {
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

        nodeptr node = info.oNode;
        nodeptr parent = info.oParent;
        nodeptr gParent = info.oGParent;

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
            arraycopy(parent->ptrs, 0, absorber->ptrs, 0, info.nodeIndex);
            arraycopy(node->ptrs, 0, absorber->ptrs, info.nodeIndex, nsize);
            arraycopy(parent->ptrs, info.nodeIndex + 1, absorber->ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));

            arraycopy(parent->keys, 0, absorber->keys, 0, info.nodeIndex);
            arraycopy(node->keys, 0, absorber->keys, info.nodeIndex, getKeyCount(node));
            arraycopy(parent->keys, info.nodeIndex, absorber->keys, info.nodeIndex + getKeyCount(node), getKeyCount(parent) - info.nodeIndex);
            //hate this
            absorber->searchKey = absorber->keys[0]; // TODO: verify this is same as in llx/scx abtree

            gParent->ptrs[info.parentIndex] = absorber;
            RECLAIM_NODE(node);
            RECLAIM_NODE(parent);

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
            arraycopy(parent->ptrs, 0, ptrs, 0, info.nodeIndex);
            arraycopy(node->ptrs, 0, ptrs, info.nodeIndex, nsize);
            arraycopy(parent->ptrs, info.nodeIndex + 1, ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));
            arraycopy(parent->keys, 0, keys, 0, info.nodeIndex);
            arraycopy(node->keys, 0, keys, info.nodeIndex, getKeyCount(node));
            arraycopy(parent->keys, info.nodeIndex, keys, info.nodeIndex + getKeyCount(node), getKeyCount(parent) - info.nodeIndex);

            // the new arrays are too big to fit in a single node,
            // so we replace p by a new internal node and two new children.
            //
            // we take the big merged array and split it into two arrays,
            // which are used to create two new children u and v.
            // we then create a new internal node (whose weight will be zero
            // if it is not the root), with u and v as its children.

            // create new node(s)
            const int leftSize = size / 2;
            nodeptr left = createInternalNode(tid, true, leftSize, keys[0]);
            arraycopy(keys, 0, left->keys, 0, leftSize - 1);
            arraycopy(ptrs, 0, left->ptrs, 0, leftSize);

            const int rightSize = size - leftSize;
            nodeptr right = createInternalNode(tid, true, rightSize, keys[leftSize]);
            arraycopy(keys, leftSize, right->keys, 0, rightSize - 1);
            arraycopy(ptrs, leftSize, right->ptrs, 0, rightSize);

            // note: keys[leftSize - 1] should be the same as n->keys[0]
            nodeptr newNode = createInternalNode(tid, gParent == entry, 2, keys[leftSize - 1]);
            newNode->keys[0] = keys[leftSize - 1];
            newNode->ptrs[0] = left;
            newNode->ptrs[1] = right;

            // note: weight of new internal node n will be zero,
            //       unless it is the root; this is because we test
            //       gp == entry, above; in doing this, we are actually
            //       performing Root-Zero at the same time as this Overflow
            //       if n will become the root

            gParent->ptrs[info.parentIndex] = newNode;
            RECLAIM_NODE(node);
            RECLAIM_NODE(parent);

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

template <typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::fixDegreeViolation(const int tid, nodeptr viol) {
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

        if (parent->size == 1) {
            return false; // p has only one child, so we cannot do absorbSibling or distribute... must be resolved at a higher level
            // in theory might want to search for & fix the corresponding degree violation one step above us rather than returning. in practice this choice should tend to be faster...
            // note: this bug was found because of segfaults found indepedently by Ajay Singh and Pedro Ramalhete.
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
                        newNodeExt->keys[keyCounter++] = left->keys[i];
                        newNodeExt->ptrs[ptrCounter++] = left->ptrs[i];
                    }
                }
                assert(right->leaf);
                for (int i = 0; i < DEGREE; i++) {
                    if (right->keys[i]) {
                        newNodeExt->keys[keyCounter++] = right->keys[i];
                        newNodeExt->ptrs[ptrCounter++] = right->ptrs[i];
                    }
                }
                newNode = newNodeExt;
            } else {
                nodeptr newNodeInt = createInternalNode(tid, true, size, node->searchKey);
                for (int i = 0; i < getKeyCount(left); i++) {
                    newNodeInt->keys[keyCounter++] = left->keys[i];
                }
                newNodeInt->keys[keyCounter++] = parent->keys[leftIndex];
                for (int i = 0; i < lsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = left->ptrs[i];
                }
                assert(!right->leaf);
                for (int i = 0; i < getKeyCount(right); i++) {
                    newNodeInt->keys[keyCounter++] = right->keys[i];
                }
                for (int i = 0; i < rsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = right->ptrs[i];
                }
                newNode = newNodeInt;
            }

            // now, we atomically replace p and its children with the new nodes.
            // if appropriate, we perform RootAbsorb at the same time.
            if (gParent == entry && psize == 2) {
                gParent->ptrs[info.parentIndex] = newNode;
                RECLAIM_NODE(node);
                RECLAIM_NODE(parent);
                RECLAIM_NODE(sibling);
                fixDegreeViolation(tid, newNode);
                return RetCode::SUCCESS;
// cleanup_retry0:
//                 DEALLOCATE_NODE(newNode);
            } else {
                assert(gParent != entry || psize > 2);
                // create n from p by:
                // 1. skipping the key for leftindex and child pointer for ixToS
                // 2. replacing l with newl
                nodeptr newParent = createInternalNode(tid, true, psize - 1, parent->searchKey);
                for (int i = 0; i < leftIndex; i++) {
                    newParent->keys[i] = parent->keys[i];
                }
                for (int i = 0; i < siblingIndex; i++) {
                    newParent->ptrs[i] = parent->ptrs[i];
                }
                for (int i = leftIndex + 1; i < getKeyCount(parent); i++) {
                    newParent->keys[i - 1] = parent->keys[i];
                }
                for (int i = info.nodeIndex + 1; i < psize; i++) {
                    newParent->ptrs[i - 1] = parent->ptrs[i];
                }

                // replace l with newl in n's pointers
                newParent->ptrs[info.nodeIndex - (info.nodeIndex > siblingIndex)] = newNode;

                gParent->ptrs[info.parentIndex] = newParent;
                RECLAIM_NODE(node);
                RECLAIM_NODE(parent);
                RECLAIM_NODE(sibling);
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

            kvpair<K> tosort[DEGREE * 2];

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

            if (left->leaf) qsort(tosort, keyCounter, sizeof(kvpair<K>), kv_compare<K>);

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
                nodeptr newRightExt = createExternalNode(tid, true, rightSize, (K)0x0);
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
            nodeptr newParent = createInternalNode(tid, parent->weight, psize, parent->searchKey);
            arraycopy(parent->keys, 0, newParent->keys, 0, getKeyCount(parent));
            arraycopy(parent->ptrs, 0, newParent->ptrs, 0, psize);
            newParent->ptrs[leftIndex] = newLeft;
            newParent->ptrs[rightIndex] = newRight;
            newParent->keys[leftIndex] = pivot;

            gParent->ptrs[info.parentIndex] = newParent;
            RECLAIM_NODE(node);
            RECLAIM_NODE(sibling);
            RECLAIM_NODE(parent);
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


#ifdef PHTYTM_ENABLE_RANGE_QUERY

template <typename K, typename V, int DEGREE>
inline int ABTreeTM<K, V, DEGREE>::getKeys(const int tid, nodeptr node, K * const outputKeys, void ** const outputValues) {
    if (node->leaf) {
        // leaf ==> its keys are in the set.
        const int sz = getKeyCount(node);
        for (int i=0;i<sz;++i) {
            outputKeys[i] = node->keys[i];
            outputValues[i] = (void *) node->ptrs[i];
        }
        return sz;
    }
    // note: internal ==> its keys are NOT in the set
    return 0;
}

template <typename K, typename V, int DEGREE>
inline bool ABTreeTM<K, V, DEGREE>::isInRange(const K& key, const K& lo, const K& hi) {
    return (key >= lo && hi >= key);
}

template <typename K, typename V, int DEGREE>
inline void ABTreeTM<K, V, DEGREE>::traversal_try_add(const int tid, nodeptr const node, K * const rqResultKeys, V * const rqResultValues, int * const startIndex, const K& lo, const K& hi) {
        int start = (*startIndex);
        int keysInNode = getKeys(tid, node, rqResultKeys+start, rqResultValues+start);
        if (keysInNode == 0) return;
        int location = start; 
        for (int i=start;i<keysInNode+start;++i) {
            if (isInRange(rqResultKeys[i], lo, hi)){
                rqResultKeys[location] = rqResultKeys[i];
                rqResultValues[location] = rqResultValues[i];
                ++location;
            }   
        }
        *startIndex = location;
#if defined MICROBENCH
        assert(*startIndex <= RQSIZE);
#endif
}

template<typename K, typename V, int DEGREE>
int ABTreeTM<K, V, DEGREE>::rangeQuery(const int tid, const K& lo, const K& hi, K * const resultKeys, void ** const resultValues) {
    // block<Node<K,V,DEGREE>> stack (NULL);
    std::vector<nodeptr> stack;
    // depth first traversal (of interesting subtrees)
    int size = 0;

    stack.push_back(entry);
    while (!stack.empty()) {
        nodeptr node = stack.back();
        stack.pop_back();

        // if leaf node, check if we should add its keys to the traversal
        if (node->leaf) {
            traversal_try_add(tid, node, resultKeys, resultValues, &size, lo, hi);        
        } else { // else if internal node, explore its children
            // find right-most sub-tree that could contain a key in [lo, hi]
            int nkeys = getKeyCount(node);
            int r = nkeys;
            while (r > 0 && hi < node->keys[r-1]) {
                --r;           // subtree rooted at node->ptrs[r] contains only keys > hi
            }

            // find left-most sub-tree that could contain a key in [lo, hi]
            int l = 0;
            while (l < nkeys && lo >= node->keys[l]) {
                ++l;        // subtree rooted at node->ptrs[l] contains only keys < lo
            }

            // perform DFS from left to right (so push onto stack from right to left)
            for (int i=r;i>=l; --i) {
                stack.push_back(node->ptrs[i]);
            }

//            // simply explore EVERYTHING
//            for (int i=0;i<node->getABDegree();++i) {
//                stack.push(rqProvider->read_addr(tid, &node->ptrs[i]));
//            }
        }
    }

    // success
    return size;
}
#endif

template <typename K, typename V, int DEGREE>
void ABTreeTM<K, V, DEGREE>::printDebuggingDetails() {
}


template <typename K, typename V, int DEGREE>
long ABTreeTM<K, V, DEGREE>::validateSubtree(nodeptr node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound) {

    if (node == NULL) return 0;
    // graph << "\"" << node << "\"" << "[label=\"K: " << node->searchKey << " - W: "
    //         << node->weight << " - L: " << node->leaf << " - N: " << node << "\"];\n";
    graph << "\"" << node << "\"" << "[shape=record, label=\"S" << node->searchKey << " | W"
            << node->weight << " | L" << node->leaf; //<< "\"];\n";
    int last = -1;
    if (node->leaf) {
        for (int i = 0; i < DEGREE; i++) {
            K key = node->keys[i];
            graph << " | <k"<<i<<">";
            if (key) graph << key; else graph << "x";
        }
    } else {
        for (int i = 0; i < node->size-1; i++) {
            K key = node->keys[i];
            graph << " | <p"<<i<<">";
            graph << " | <k"<<i<<">";
            if (key) graph << key; else graph << "x";
        }
        graph << " | <p"<<(node->size-1)<<">";
    }
    graph << " \"];\n";

    if (!node->weight) {
        log << "Weight Violation! " << node->searchKey << "\n";
        errorFound = true;
    }

    if (node->leaf) {
        for (int i = 0; i < DEGREE; i++) {
            auto leaf = node;
            K key = leaf->keys[i];
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
            graph << "\"" << node << "\":<p"<<i<<"> -> \"" << node->ptrs[i] << "\";\n";
            validateSubtree(node->ptrs[i], keys, graph, log, errorFound);
        }
    }

    return errorFound;
    // return 1;
}

template <typename K, typename V, int DEGREE>
bool ABTreeTM<K, V, DEGREE>::validate() {
    // return true;

    fflush(stdout);
    std::unordered_set<K> keys = {};
    bool errorFound;

    rename("graph.dot", "graph_before.dot");
    ofstream graph;
    graph.open("graph.dot");
    graph << "digraph G {\n";

    ofstream log;
    log.open("log.txt", std::ofstream::out);

    auto t = std::time(nullptr);
    auto tm = *std::localtime(&t);
    log << "Run at: " << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << "\n";

    long ret = validateSubtree(entry, keys, graph, log, errorFound);
    graph << "}";
    graph.flush();

    graph.close();

    if (!errorFound) {
        log << "Validated Successfully!\n";
    }
    log.flush();

    log.close();
    fflush(stdout);
    return !errorFound;    
}
