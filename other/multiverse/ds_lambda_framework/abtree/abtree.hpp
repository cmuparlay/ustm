#pragma once

/*
Larsen relaxed a,b-tree

In this implementation weight === !tag in other words if a node has weight = 1
(or tag = 1 if you prefer the larsen description) then the node IS counted towards the
depth/height. This is a bit different to larsen who let nodes with tag = 0 represent 'real' nodes

This implementation is converted from the a,b-tree of Trevor Brown which
originally utilized LLX/SCX as the synchronizaiton primitive.

TODO: remove the 'leftovers'
    There are some things 'leftover' from this conversion, in particular the
    return values of many of the rebalancing functions are an enum that suggest
    it is possible to fail performing a rebalance step. Since the updates
    are wrapped in transactions this is not the case however this is a negligable overhead
*/

#include <cassert>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "../../ds_common/common.h"

using namespace std;

template <typename K, typename V>
class ABTreeTM {
    PAD;

private:
    class Node {
        public:
        TM_TYPE_T<int> size;         // TREE_DEGREE of node
        TM_TYPE_T<K> keys[TREE_DEGREE];
        TM_TYPE_T<Node*> ptrs[TREE_DEGREE]; // also doubles as a spot for VALUES
        K searchKey;    // key that can be used to find this node (even if its empty) // const
        int weight;     // 0 or 1 // const
        bool leaf;      // 0 or 1 // const

        Node (int _size, K _searchKey, int _weight, bool _leaf) {
            size = (_size);
            leaf = (_leaf);
            weight = (_weight);
            searchKey = (_searchKey);
            for (int i=0;i<TREE_DEGREE;++i) {
                keys[i] = (EMPTY_KEY);
                ptrs[i] = (nullptr);
            }
        }

        // void destroy() {
        //     for (int i = 0; i < TREE_DEGREE; i++) {
        //         keys[i] = (EMPTY_KEY);
        //         ptrs[i] = (nullptr);
        //     }
        // }
    };

    struct SearchInfo {
        Node* oNode;
        Node* oParent;
        Node* oGParent;
        int parentIndex = 0;
        int nodeIndex = 0;
        int keyIndex = 0;
        V val;
    };

public:
    PAD;
    TM_TYPE_T<Node*> entry;
    PAD;
    const int a;
    const int b;
    PAD;

    ABTreeTM() : b(TREE_DEGREE), a(max(TREE_DEGREE / 4, 2)) {
        assert(sizeof(V) == sizeof(Node*));
        assert(SUCCESS == RetCode::SUCCESS);
        assert(RETRY == RetCode::RETRY);

        TM_T::updateTx ([&] {
            // initial tree: entry is a sentinel node (with one pointer and no keys)
            // that points to an empty node (no pointers and no keys)
            Node* _entryLeft = createExternalNode(true, 0, EMPTY_KEY);

            //sentinel node
            Node* _entry = createInternalNode(true, 1, EMPTY_KEY);
            _entry->ptrs[0] = _entryLeft;

            entry = _entry;
        });
        deinitThread();
    }

    ~ABTreeTM() {
        TM_T::updateTx ([&] {
            deleteSubtree(entry);
        });
    }

    void deinitThread() {
        TM_NS::thread_registry_deregister_thread();
    }

    bool contains(const K &key) {
        bool retVal = false;
        TM_T::readTx ([&] {
            retVal = searchBasic(key) != NO_VAL;
        });
        return retVal;
    }

    V get(const K &key) {
        V retVal = NO_VAL;
        TM_T::readTx ([&] {
            retVal = searchBasic(key);
        });
        return retVal;
    }

    V addKey(const K &key, const V &value) {
        SearchInfo info;
        V retVal = NO_VAL;
        TM_T::template updateTx<V> ([&] {
            int res = search(info, key);
            if (res == RetCode::SUCCESS) {
                retVal = info.val;
                return retVal;
            }
            if (insert(info, key, value)) {
                retVal = NO_VAL;
                return retVal;
            }
            retVal = NO_VAL;
            return retVal;
        });
        return retVal;
    }

    V removeKey(const K &key) {
        SearchInfo info;
        V retVal = NO_VAL;
        TM_T::template updateTx<V> ([&] {
            int res = search(info, key);
            if (res == RetCode::FAILURE) {
                retVal = NO_VAL;
                return retVal;
            }
            if (erase(info, key)) {
                retVal = info.val;
                return retVal;
            }
            retVal = NO_VAL;
            return retVal;
        });
        return retVal;
    }

    void getSuccessorKey(const K& key, K* successorKey, V* successorVal) {
        SearchInfo info;
        K succKeyTmp;
        VAL_T succValTmp;
        TM_T::template readTx<bool> ([&] {
            info.oGParent = nullptr;
            info.parentIndex = 0;
            info.keyIndex = 0;

            info.oParent = entry.load();
            info.nodeIndex = 0;
            info.oNode = entry->ptrs[0].load();

            while (!(info.oNode->leaf)) {
                assert(info.oNode->size >= 0 && info.oNode->size <= TREE_DEGREE);
                assert(info.oParent->size >= 0 && info.oParent->size <= TREE_DEGREE);
                assert(!info.oGParent || (info.oGParent && info.oGParent->size >= 0 && info.oGParent->size <= TREE_DEGREE));
                info.oGParent = info.oParent;
                info.oParent = info.oNode;
                info.parentIndex = info.nodeIndex;
                info.nodeIndex = getChildIndex(info.oNode, key);
                info.oNode = info.oNode->ptrs[info.nodeIndex].load();
            }

            info.keyIndex = getKeyIndex(info.oNode, key);
            if (info.keyIndex < TREE_DEGREE) {
                info.val = (VAL_T)(info.oNode->ptrs[info.keyIndex].load());
            }
            else {
                for (int i = 0; i < TREE_DEGREE; i++) {
                    if (i != info.keyIndex && info.oNode->keys[info.keyIndex] != EMPTY_KEY) {
                        info.keyIndex = i;
                        break;
                    }
                }
            }

            succKeyTmp = info.oNode->keys[info.keyIndex];
            succValTmp = (VAL_T)(info.oNode->ptrs[info.keyIndex].load());
        });
        *successorKey = succKeyTmp;
        *successorVal = succValTmp;
    }

    void update(const K& key, VAL_T value) {
        SearchInfo info;
        TM_T::updateTx ([&] {
            int res = search(info, key);
            if (res == RetCode::SUCCESS) {
                info.oNode->ptrs[info.keyIndex] = (Node*)value;
            }
            else {
                insert(info, key, value);
            }
        });
    }

    void update_insert_delete(const K& key, VAL_T value) {
        SearchInfo info;
        TM_T::updateTx ([&] {
            int res = search(info, key);
            if (res == RetCode::SUCCESS) {
                erase(info, key);
            }
            else {
                insert(info, key, value);
            }
        });
    }

    bool validate();
    bool validateSubtree(Node* node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound);
    int rangeQuery(const K& lo, const K& hi, K * const resultKeys, void ** const resultValues);

  private:
    inline int getKeyCount(Node* node) {
        int sz = node->size.load();
        if (!(node->leaf)) {
            sz -= 1;
        }
        return sz;
        // return node->leaf ? node->size : node->size - 1;
    }


    inline int getChildIndex(Node* node, const K &key) {
        int nkeys = getKeyCount(node);
        int retval = 0;
        K nodeKey = node->keys[retval].load();
        while (retval < nkeys && key >= nodeKey) {
            ++retval;
            nodeKey = node->keys[retval].load();
        }
        return retval;
    }

    inline int getKeyIndex(Node* node, const K &key) {
        int idx = 0;
        while (idx < TREE_DEGREE && node->keys[idx].load() != key) {
            ++idx;
        }
        return idx;
    }

    Node* createInternalNode(bool weight, int size, K searchKey) {
        Node* node = ALLOC_NODE(size, searchKey, weight, 0);
        return node;
    }
    Node* createExternalNode(bool weight, int size, K searchKey) {
        Node* node = createInternalNode(weight, size, searchKey);
        node->leaf = (1);
        return node;
    }

    int erase(SearchInfo &info, const K &key);
    int insert(SearchInfo &info, const K &key, const V &value);
    V searchBasic(const K &key);
    int search(SearchInfo &info, const K &key, Node* target = NULL);
    int searchTarget(SearchInfo &info, Node* target, const K &key);
    int fixDegreeViolation(Node* viol);
    int fixWeightViolation(Node* viol);
    void traversal_try_add(Node* const node, K * const rqResultKeys, V * const rqResultValues, int * const startIndex, const K& lo, const K& hi);
    int getKeys(Node* node, K * const outputKeys, void ** const outputValues);
    bool isInRange(const K& key, const K& lo, const K& hi);

    void deleteSubtree(Node* root) {
        if (root->leaf) {
            RECLAIM_NODE(root);
            return;
        }
        for (int i = 0; i < root->size; i++) {
            deleteSubtree(root->ptrs[i]);
        }
        RECLAIM_NODE(root);
    }
};



/* searchBasic(const K &key)
 * Basic search, returns respective value associated with key, or NO_VAL if nothing is found
 * does not return any path information like other searches (and is therefore slightly faster)
 * called by contains() and find()
 */
template <typename K, typename V>
V ABTreeTM<K, V>::searchBasic(const K &key) {
    while (true) {
        Node* node = entry->ptrs[0].load();
        Node* parent = entry;
        while (!node->leaf) {
            parent = node;
            node = node->ptrs[getChildIndex(node, key)].load();
        }
        int keyIndex = getKeyIndex(node, key);
        if (keyIndex < TREE_DEGREE) {
            return (V)(node->ptrs[keyIndex].load());
        }
        else return NO_VAL;
    }
}

/* search(SearchInfo &info, const K &key)
 * normal search used to search for a specific key, fills a SearchInfo struct so the caller
 * can manipulate the nodes around the searched for key
 */
template <typename K, typename V>
int ABTreeTM<K, V>::search(SearchInfo &info, const K &key, Node* target) {
    info.oGParent = nullptr;
    info.parentIndex = 0;
    info.keyIndex = 0;

    info.oParent = entry.load();
    info.nodeIndex = 0;
    info.oNode = entry->ptrs[0].load();

    while (!(info.oNode->leaf) && (target ? info.oNode != target : true)) {
        assert(info.oNode->size >= 0 && info.oNode->size <= TREE_DEGREE);
        assert(info.oParent->size >= 0 && info.oParent->size <= TREE_DEGREE);
        assert(!info.oGParent || (info.oGParent && info.oGParent->size >= 0 && info.oGParent->size <= TREE_DEGREE));
        info.oGParent = info.oParent;
        info.oParent = info.oNode;
        info.parentIndex = info.nodeIndex;
        info.nodeIndex = getChildIndex(info.oNode, key);
        info.oNode = info.oNode->ptrs[info.nodeIndex].load();
    }
    if (target) {
        if (info.oNode == target) return RetCode::SUCCESS;
        return RetCode::FAILURE;
    } else {
        info.keyIndex = getKeyIndex(info.oNode, key);
        // if key found
        if (info.keyIndex < TREE_DEGREE) {
            info.val = (V)(info.oNode->ptrs[info.keyIndex].load());
            return RetCode::SUCCESS;
        }
        return RetCode::FAILURE;
    }
}

/* searchTarget(SearchInfo &info, Node* target, const K &key)
 * Searches for a key, however halts when a specific target node is reached. Return
 * is dependent on if the node halted at is the target (indicating the key searched for leads, at some point,
 * to this node)
 */
template <typename K, typename V>
int ABTreeTM<K, V>::searchTarget(SearchInfo &info, Node* target, const K &key) {
    return search(info, key, target);
}


template <typename K, typename V>
int ABTreeTM<K, V>::insert(SearchInfo &info, const K &key, const V &value) {
    Node* node = info.oNode;
    Node* parent = info.oParent;
    int nodeSize = node->size.load();
    if (nodeSize < b) {
        //we have the capacity to fit this new key
        // find empty slot
        for (int i = 0; i < TREE_DEGREE; ++i) {
            if (node->keys[i].load() == (K) 0) {
                node->keys[i].store(key);
                node->ptrs[i].store((Node*) value);
                node->size.store(1+nodeSize);
                fixDegreeViolation(node);
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
        kvpair<K> tosort[TREE_DEGREE + 1];
        int k = 0;
        for (int i = 0; i < TREE_DEGREE; i++) {
            if (node->keys[i].load()) {
                tosort[k].key = node->keys[i].load();
                tosort[k].val = node->ptrs[i].load();
                ++k;
            }
        }
        tosort[k].key = key;
        tosort[k].val = value;
        ++k;
        qsort(tosort, k, sizeof(kvpair<K>), kv_compare_single_col_key<K>);

        // create new node(s):
        // since the new arrays are too big to fit in a single node,
        // we replace l by a new subtree containing three new nodes:
        // a parent, and two leaves;
        // the array contents are then split between the two new leaves

        const int leftSize = k / 2;
        Node* left = createExternalNode(true, leftSize, tosort[0].key);
        for (int i = 0; i < leftSize; i++) {
            left->keys[i].store((tosort[i].key));
            left->ptrs[i].store(((Node*)tosort[i].val));
        }

        const int rightSize = (TREE_DEGREE + 1) - leftSize;
        Node* right = createExternalNode(true, rightSize, tosort[leftSize].key);
        for (int i = 0; i < rightSize; i++) {
            right->keys[i].store((tosort[i + leftSize].key));
            right->ptrs[i].store(((Node*)tosort[i + leftSize].val));
        }

        Node* replacementNode = createInternalNode(parent == entry.load(), 2, tosort[leftSize].key);
        replacementNode->keys[0].store((tosort[leftSize].key));
        replacementNode->ptrs[0].store((left));
        replacementNode->ptrs[1].store((right));

        // note: weight of new internal node n will be zero,
        //       unless it is the root; this is because we test
        //       p == entry, above; in doing this, we are actually
        //       performing Root-Zero at the same time as this Overflow
        //       if n will become the root

        parent->ptrs[info.nodeIndex] = replacementNode;
        fixWeightViolation(replacementNode);
        RECLAIM_NODE(node);
        return RetCode::SUCCESS;
    }
    assert(false);
}


template <typename K, typename V>
int ABTreeTM<K, V>::erase(SearchInfo &info, const K &key) {
    Node* node = info.oNode;
    Node* parent = info.oParent;
    Node* gParent = info.oGParent;
    assert(node->leaf);
    assert(!(parent->leaf));
    assert(gParent == NULL || !(gParent->leaf));
    int currSize = node->size;
    node->keys[info.keyIndex].store((K)EMPTY_KEY);
    node->size.store(currSize-1);
    assert(currSize - 1 >= 0);
    fixDegreeViolation(node);
    return RetCode::SUCCESS;
}

template <typename K, typename V>
int ABTreeTM<K, V>::fixWeightViolation(Node* viol) {
    int debugDepth = -1;
    while (true) {
        debugDepth++;
        if (viol->weight) {
            return RetCode::UNNECCESSARY;
        }

        // assert: viol is internal (because leaves always have weight = 1)
        assert(!viol->leaf);
        // assert: viol is not entry or root (because both always have weight = 1)
        assert(viol != entry && viol != entry->ptrs[0].load());


        SearchInfo info;
        searchTarget(info, viol, viol->searchKey);

        Node* node = info.oNode;
        Node* parent = info.oParent;
        Node* gParent = info.oGParent;

        if (node != viol) {
            return RetCode::UNNECCESSARY;
        }

        // we cannot apply this update if p has a weight violation
        // so, we check if this is the case, and, if so, try to fix it
        if (!parent->weight) {
            fixWeightViolation(parent);
            continue;
        }

        const int psize = parent->size;
        const int nsize = viol->size;
        const int c = psize + nsize;
        const int size = c - 1;
        assert(size >= 0);

        if (size <= b) {
            assert(!node->leaf);
            /**
             * Absorb
             */

            // create new node(s)
            // the new arrays are small enough to fit in a single node,
            // so we replace p by a new internal node.
            Node* absorber = createInternalNode(true, size, (K)EMPTY_KEY);

            assert(info.nodeIndex < TREE_DEGREE+1);
            assert(nsize < TREE_DEGREE+1);
            assert(psize - (info.nodeIndex + 1) < TREE_DEGREE+1);
            tmarraycopy(parent->ptrs, 0, absorber->ptrs, 0, info.nodeIndex);
            tmarraycopy(node->ptrs, 0, absorber->ptrs, info.nodeIndex, nsize);
            tmarraycopy(parent->ptrs, info.nodeIndex + 1, absorber->ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));

            assert(0 <= getKeyCount(parent) - info.nodeIndex);
            assert(getKeyCount(parent) - info.nodeIndex < TREE_DEGREE+1);

            tmarraycopy(parent->keys, 0, absorber->keys, 0, info.nodeIndex);
            tmarraycopy(node->keys, 0, absorber->keys, info.nodeIndex, getKeyCount(node));
            tmarraycopy(parent->keys, info.nodeIndex, absorber->keys, info.nodeIndex + getKeyCount(node), getKeyCount(parent) - info.nodeIndex);

            absorber->searchKey = (absorber->keys[0].load());

            assert(gParent);
            // assert(gParent != entry);
            gParent->ptrs[info.parentIndex] = absorber;

            RECLAIM_NODE(parent);
            RECLAIM_NODE(node);
            fixDegreeViolation(absorber);

            return RetCode::SUCCESS;
        } else {
            assert(!node->leaf);
            /**
             * Split
             */

            // merge keys of p and l into one big array (and similarly for children)
            // (we essentially replace the pointer to l with the contents of l)
            K keys[2 * TREE_DEGREE];
            Node* ptrs[2 * TREE_DEGREE];

            assert(info.nodeIndex-1 < TREE_DEGREE);
            assert(nsize-1 < TREE_DEGREE);
            assert((info.nodeIndex + 1) + (psize - (info.nodeIndex + 1))-1 < TREE_DEGREE);
            tmarraycopy(parent->ptrs, 0, ptrs, 0, info.nodeIndex);
            tmarraycopy(node->ptrs, 0, ptrs, info.nodeIndex, nsize);
            tmarraycopy(parent->ptrs, info.nodeIndex + 1, ptrs, info.nodeIndex + nsize, psize - (info.nodeIndex + 1));
            tmarraycopy(parent->keys, 0, keys, 0, info.nodeIndex);

            assert(0 <= getKeyCount(parent) - info.nodeIndex);
            assert(getKeyCount(parent) - info.nodeIndex < TREE_DEGREE+1);

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
            Node* left = createInternalNode(true, leftSize, keys[0]);

            assert(leftSize - 1 -1 < 2*TREE_DEGREE);
            assert(leftSize -1 < 2*TREE_DEGREE);
            arraycopy(keys, 0, left->keys, 0, leftSize - 1);
            arraycopy(ptrs, 0, left->ptrs, 0, leftSize);

            const int rightSize = size - leftSize;
            Node* right = createInternalNode(true, rightSize, keys[leftSize]);

            assert(leftSize + rightSize - 1 - 1 < 2*TREE_DEGREE);
            assert(leftSize + rightSize - 1 < 2*TREE_DEGREE);
            arraycopy(keys, leftSize, right->keys, 0, rightSize - 1);
            arraycopy(ptrs, leftSize, right->ptrs, 0, rightSize);

            // note: keys[leftSize - 1] should be the same as n->keys[0]
            Node* newNode = createInternalNode(gParent == entry.load(), 2, keys[leftSize - 1]);
            newNode->keys[0] = (keys[leftSize - 1]);
            newNode->ptrs[0] = (left);
            newNode->ptrs[1] = (right);

            // note: weight of new internal node n will be zero,
            //       unless it is the root; this is because we test
            //       gp == entry, above; in doing this, we are actually
            //       performing Root-Zero at the same time as this Overflow
            //       if n will become the root

            gParent->ptrs[info.parentIndex] = newNode;
            RECLAIM_NODE(parent);
            RECLAIM_NODE(node);
            fixWeightViolation(newNode);
            fixDegreeViolation(newNode);
            return RetCode::SUCCESS;
        }
    }
    assert(false);
}

template <typename K, typename V>
int ABTreeTM<K, V>::fixDegreeViolation(Node* viol) {
    // we search for viol and try to fix any violation we find there
    // this entails performing AbsorbSibling or Distribute.

    Node* parent = NULL;
    Node* gParent = NULL;
    Node* node = NULL;
    Node* sibling = NULL;
    Node* left = NULL;
    Node* right = NULL;

    while (true) {
        if (viol->size >= a || viol == entry.load() || viol == entry->ptrs[0].load()) {
            return RetCode::UNNECCESSARY; // no degree violation at viol
        }

        /**
         * search for viol
         */
        SearchInfo info;
        searchTarget(info, viol, viol->searchKey);
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
        sibling = parent->ptrs[siblingIndex].load();

        // we can only apply AbsorbSibling or Distribute if there are no
        // weight violations at p, l or s.
        // so, we first check for any weight violations,
        // and fix any that we see.
        bool foundWeightViolation = false;
        if (!parent->weight) {
            foundWeightViolation = true;
            fixWeightViolation(parent);
        }

        if (!node->weight) {
            foundWeightViolation = true;
            fixWeightViolation(node);
        }

        if (parent->size.load() == 1) {
            return RetCode::UNNECCESSARY;
            // return false; // p has only one child, so we cannot do absorbSibling or distribute... must be resolved at a higher level
            // in theory might want to search for & fix the corresponding degree violation one step above us rather than returning. in practice this choice should tend to be faster...
            // note: this bug was found because of segfaults found indepedently by Ajay Singh and Pedro Ramalhete.
        }

        if (!sibling->weight) {
            foundWeightViolation = true;
            fixWeightViolation(sibling);
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
        assert(parent->size.load() >= a);
        assert(parent->size.load() >= 2);

        int leftIndex;
        int rightIndex;
        Node* left = NULL;
        Node* right = NULL;

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

        int lsize = left->size.load();
        int rsize = right->size.load();
        int psize = parent->size.load();
        int size = lsize+rsize;
        // assert(left->weight && right->weight); // or version # has changed

        if (size < 2 * a) {
            /**
             * AbsorbSibling
             */

            Node* newNode = NULL;
            // create new node(s))
            int keyCounter = 0, ptrCounter = 0;
            if (left->leaf) {
                //duplicate code can be cleaned up, but it would make it far less readable...
                Node* newNodeExt = createExternalNode(true, size, node->searchKey);
                for (int i = 0; i < TREE_DEGREE; i++) {
                    if (left->keys[i].load()) {
                        newNodeExt->keys[keyCounter++] = (left->keys[i].load());
                        newNodeExt->ptrs[ptrCounter++] = (left->ptrs[i].load());
                    }
                }
                assert(right->leaf);
                for (int i = 0; i < TREE_DEGREE; i++) {
                    if (right->keys[i].load()) {
                        newNodeExt->keys[keyCounter++] = (right->keys[i].load());
                        newNodeExt->ptrs[ptrCounter++] = (right->ptrs[i].load());
                    }
                }
                newNode = newNodeExt;
            } else {
                Node* newNodeInt = createInternalNode(true, size, node->searchKey);
                assert(!(left->leaf));
                // for (int i = 0; i < getKeyCount(left); i++) {
                for (int i = 0; i < lsize-1; i++) {
                    newNodeInt->keys[keyCounter++] = (left->keys[i].load());
                }
                newNodeInt->keys[keyCounter++] = (parent->keys[leftIndex].load());
                for (int i = 0; i < lsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = (left->ptrs[i].load());
                }
                assert(!right->leaf);
                // for (int i = 0; i < getKeyCount(right); i++) {
                for (int i = 0; i < rsize-1; i++) {
                    newNodeInt->keys[keyCounter++] = (right->keys[i].load());
                }
                for (int i = 0; i < rsize; i++) {
                    newNodeInt->ptrs[ptrCounter++] = (right->ptrs[i].load());
                }
                newNode = newNodeInt;
            }

            assert(keyCounter <= TREE_DEGREE);
            assert(ptrCounter <= TREE_DEGREE);

            // now, we atomically replace p and its children with the new nodes.
            // if appropriate, we perform RootAbsorb at the same time.
            if (gParent == entry.load() && psize == 2) {
                gParent->ptrs[info.parentIndex] = newNode;

                RECLAIM_NODE(parent);
                RECLAIM_NODE(node);
                RECLAIM_NODE(sibling);
                fixDegreeViolation(newNode);
                return RetCode::SUCCESS;
            } else {
                assert(gParent != entry.load() || psize > 2);
                assert(!(parent->leaf));
                // create n from p by:
                // 1. skipping the key for leftindex and child pointer for ixToS
                // 2. replacing l with newl
                Node* newParent = createInternalNode(true, psize - 1, parent->searchKey);

                assert(leftIndex-1 < TREE_DEGREE);
                for (int i = 0; i < leftIndex; i++) {
                    newParent->keys[i] = (parent->keys[i].load());
                }

                assert(siblingIndex-1 < TREE_DEGREE);
                for (int i = 0; i < siblingIndex; i++) {
                    newParent->ptrs[i] = (parent->ptrs[i].load());
                }
                // for (int i = leftIndex + 1; i < getKeyCount(parent); i++) {
                for (int i = leftIndex + 1; i < psize-1; i++) {
                    newParent->keys[i - 1] = (parent->keys[i].load());
                    assert(i-1 < TREE_DEGREE);
                }
                for (int i = info.nodeIndex + 1; i < psize; i++) {
                    newParent->ptrs[i - 1] = (parent->ptrs[i].load());
                    assert(i-1 < TREE_DEGREE);
                }

                // replace l with newl in n's pointers
                newParent->ptrs[leftIndex] = (newNode);
                assert(leftIndex < TREE_DEGREE);

                gParent->ptrs[info.parentIndex] = newParent;

                RECLAIM_NODE(parent);
                RECLAIM_NODE(node);
                RECLAIM_NODE(sibling);
                fixDegreeViolation(newNode);
                fixDegreeViolation(newParent);
                return RetCode::SUCCESS;
            }
        } else {
            /**
             * Distribute
             */

            int leftSize = size / 2;
            int rightSize = size - leftSize;

            Node* newLeft = NULL;
            Node* newRight = NULL;

            kvpair<K> tosort[TREE_DEGREE + a - 1];
            // kvpair<K> tosort[TREE_DEGREE *2];

            // combine the contents of l and s (and one key from p if l and s are internal)

            int keyCounter = 0;
            int valCounter = 0;
            if (left->leaf) {
                assert(right->leaf);
                for (int i = 0; i < TREE_DEGREE; i++) {
                    if (left->keys[i].load()) {
                        tosort[keyCounter++].key = left->keys[i].load();
                        tosort[valCounter++].val = left->ptrs[i].load();
                    }
                }
            } else {
                for (int i = 0; i < getKeyCount(left); i++) {
                    tosort[keyCounter++].key = left->keys[i].load();
                }
                for (int i = 0; i < lsize; i++) {
                    tosort[valCounter++].val = left->ptrs[i].load();
                }
            }

            if (!left->leaf) tosort[keyCounter++].key = parent->keys[leftIndex].load();

            if (right->leaf) {
                for (int i = 0; i < TREE_DEGREE; i++) {
                    if (right->keys[i].load()) {
                        tosort[keyCounter++].key = right->keys[i].load();
                        tosort[valCounter++].val = right->ptrs[i].load();
                    }
                }
            } else {
                for (int i = 0; i < getKeyCount(right); i++) {
                    tosort[keyCounter++].key = right->keys[i].load();
                }
                for (int i = 0; i < rsize; i++) {
                    tosort[valCounter++].val = right->ptrs[i];
                }
            }

            assert(keyCounter <= TREE_DEGREE + a - 1);

            if (left->leaf) qsort(tosort, keyCounter, sizeof(kvpair<K>), kv_compare_single_col_key<K>);

            keyCounter = 0;
            valCounter = 0;
            K pivot;

            if (left->leaf) {
                Node* newLeftExt = createExternalNode(true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize; i++) {
                    newLeftExt->keys[i] = (tosort[keyCounter++].key);
                    newLeftExt->ptrs[i] = ((Node*)tosort[valCounter++].val);
                }
                newLeft = newLeftExt;
                newLeft->searchKey = (newLeftExt->keys[0].load());
                pivot = tosort[keyCounter].key;

            } else {
                Node* newLeftInt = createInternalNode(true, leftSize, (K)0x0);
                for (int i = 0; i < leftSize - 1; i++) {
                    newLeftInt->keys[i] = (tosort[keyCounter++].key);
                }
                for (int i = 0; i < leftSize; i++) {
                    newLeftInt->ptrs[i] = ((Node*)tosort[valCounter++].val);
                }
                newLeft = newLeftInt;
                newLeft->searchKey = (newLeftInt->keys[0].load());
                pivot = tosort[keyCounter++].key;
            }

            // reserve one key for the parent (to go between newleft and newright))

            if (right->leaf) {
                assert(left->leaf);
                Node* newRightExt = createExternalNode(true, rightSize, (K)0x0);
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    newRightExt->keys[i] = (tosort[keyCounter++].key);
                }
                newRight = newRightExt;
                newRight->searchKey = (newRightExt->keys[0].load());

                for (int i = 0; i < rightSize; i++) {
                    newRight->ptrs[i] = ((Node*)tosort[valCounter++].val);
                }
            } else {
                Node* newRightInt = createInternalNode(true, rightSize, (K)0x0);
                for (int i = 0; i < rightSize - !left->leaf; i++) {
                    newRightInt->keys[i] = (tosort[keyCounter++].key);
                }
                newRight = newRightInt;
                newRight->searchKey = (newRightInt->keys[0].load());
                for (int i = 0; i < rightSize; i++) {
                    newRight->ptrs[i] = ((Node*)tosort[valCounter++].val);
                }
            }

            // in this case we replace the parent, despite not having to in the llx/scx version...
            // this is a holdover from kcas. experiments show this case almost never occurs, though, so perf impact is negligible.
            Node* newParent = createInternalNode(parent->weight, psize, parent->searchKey);
            tmarraycopy(parent->keys, 0, newParent->keys, 0, getKeyCount(parent));
            tmarraycopy(parent->ptrs, 0, newParent->ptrs, 0, psize);
            newParent->ptrs[leftIndex] = (newLeft);
            newParent->ptrs[rightIndex] = (newRight);
            newParent->keys[leftIndex] = (pivot);

            gParent->ptrs[info.parentIndex] = newParent;
            RECLAIM_NODE(parent);
            RECLAIM_NODE(node);
            RECLAIM_NODE(sibling);
            fixDegreeViolation(newParent);
            return RetCode::SUCCESS;
        }
    }
    assert(false);
}


template <typename K, typename V>
inline int ABTreeTM<K, V>::getKeys(Node* node, K * const outputKeys, void ** const outputValues) {
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

template <typename K, typename V>
inline bool ABTreeTM<K, V>::isInRange(const K& key, const K& lo, const K& hi) {
    return (key >= lo && hi >= key);
}

template <typename K, typename V>
inline void ABTreeTM<K, V>::traversal_try_add(Node* const node, K * const rqResultKeys, V * const rqResultValues, int * const startIndex, const K& lo, const K& hi) {
        int start = (*startIndex);
        int keysInNode = getKeys(node, rqResultKeys+start, rqResultValues+start);
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
}

template<typename K, typename V>
int ABTreeTM<K, V>::rangeQuery(const K& lo, const K& hi, K * const resultKeys, void ** const resultValues) {
    int size = 0;
    std::vector<Node*> stack;
    TM_T::readTx ([&] {
        stack.clear();
        size = 0;

        // depth first traversal (of interesting subtrees)
        stack.push_back(entry.load());
        while (!stack.empty()) {
            Node* node = stack.back();
            stack.pop_back();

            // if leaf node, check if we should add its keys to the traversal
            if (node->leaf) {
                traversal_try_add(node, resultKeys, resultValues, &size, lo, hi);
            } else { // else if internal node, explore its children
                // find right-most sub-tree that could contain a key in [lo, hi]
                int nkeys = getKeyCount(node);
                assert(nkeys < TREE_DEGREE);
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
            }
        }
    });
    return size;
}



template <typename K, typename V>
bool ABTreeTM<K, V>::validateSubtree(Node* node, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound) {
    printf("VALIDATION DISABLED ALWAYS RETURNING TRUE\n");
    return true;
    // if (node == NULL) return 0;
    // graph << "\"" << node << "\"" << "[shape=record, label=\"S" << node->searchKey.load() << " | W" << node->weight.load() << " | L" << node->leaf.load(); //<< "\"];\n";
    // int last = -1;
    // if (node->leaf.load()) {
    //     for (int i = 0; i < TREE_DEGREE; i++) {
    //         K key = node->keys[i].load();
    //         graph << " | <k"<<i<<">";
    //         if (key) graph << key; else graph << "x";
    //     }
    // } else {
    //     for (int i = 0; i < node->size.load()-1; i++) {
    //         K key = node->keys[i].load();
    //         graph << " | <p"<<i<<">";
    //         graph << " | <k"<<i<<">";
    //         if (key) graph << key; else graph << "x";
    //     }
    //     graph << " | <p"<<(node->size.load()-1)<<">";
    // }
    // graph << " \"];\n";

    // if (!node->weight.load()) {
    //     // log << "Weight Violation! " << node->searchKey.load() << "\n";
    //     printf("Weight Violation!, %lld\n", node->searchKey.load());
    //     errorFound = true;
    // }
    // if (node->leaf.load()) {
    //     for (int i = 0; i < TREE_DEGREE; i++) {
    //         auto leaf = node;
    //         K key = leaf->keys[i].load();
    //         if (key) {
    //             graph << "\"" << node << "\" -> \"" << key << "\";\n";
    //             if (key < 0 || key > MAXKEY) {
    //                 // log << "Suspected pointer in leaf! " << node->searchKey.load() << "\n";
    //                 printf("Suspected pointer in leaf!, %lld\n", node->searchKey.load());
    //                 errorFound = true;
    //             }
    //             if (keys.count(key) > 0) {
    //                 // log << "DUPLICATE KEY! " << node->searchKey.load() << "\n";
    //                 printf("DUPLICATE KEY!, %lld\n", node->searchKey.load());
    //                 errorFound = true;
    //             }
    //             keys.insert(key);
    //         }
    //     }
    // }
    // else {
    //     for (int i = 0; i < node->size.load(); i++) {
    //         graph << "\"" << node << "\":<p"<<i<<"> -> \"" << node->ptrs[i].load() << "\";\n";
    //         validateSubtree(node->ptrs[i].load(), keys, graph, log, errorFound);
    //     }
    // }
    // return errorFound;
}

template <typename K, typename V>
bool ABTreeTM<K, V>::validate() {
    return true;
    // while (!__sync_bool_compare_and_swap(&validationGlobalLock, 0, 1)) {
    // }

    // fflush(stdout);
    // std::unordered_set<K> keys = {};
    // bool errorFound = false;

    // ofstream graph;
    // switch (tid) {
    //     case 0:
    //         rename("graph0.dot", "graph0_before.dot");
    //         graph.open("graph0.dot");
    //     break;
    //     case 1:
    //         rename("graph1.dot", "graph1_before.dot");
    //         graph.open("graph1.dot");
    //     break;
    //         rename("graph2.dot", "graph2_before.dot");
    //         graph.open("graph2.dot");
    //     case 2:
    //     break;
    // }
    // graph << "digraph G {\n";

    // ofstream log;
    // // log.open("log.txt", std::ofstream::out);

    // // auto t = std::time(nullptr);
    // // auto tm = *std::localtime(&t);
    // // log << "Run at: " << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << "\n";

    // bool ret = validateSubtree(entry.load(), keys, graph, log, errorFound);
    // graph << "}";
    // graph.flush();

    // graph.close();

    // if (!errorFound) {
    //     // log << "ABTree Validated Successfully!\n";
    //     printf("ABTree validated successfully\n");
    // }
    // // log.flush();

    // // log.close();
    // // fflush(stdout);

    // if (errorFound) {
    //     printf("ABTree validation failed error found\n");
    //     exit(-1);
    // }

    // // validationGlobalLock = 0;

    // return !errorFound;
}