#pragma once

#include "tm.h"

#include <cassert>

#ifndef MAX_THREADS_POW2
#define MAX_THREADS_POW2 512
#endif
#ifndef PAD
#define CAT2(x, y) x##y
#define CAT(x, y) CAT2(x, y)
#define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

#define nodeptr Node<K,V>*

template <typename K, typename V>
class Node {
  public:
    TM_TYPE_T<K> key;
    TM_TYPE_T<nodeptr> left;
    TM_TYPE_T<nodeptr> right;
    TM_TYPE_T<nodeptr> parent;
    TM_TYPE_T<int64_t> height;
    TM_TYPE_T<V> value;
};

template <typename K, typename V, class Compare>
class brown_sigouin_int_avl_tm_auto {
private:
PAD;
    nodeptr root;
    Compare cmp;
PAD;

public:
    const K NO_KEY;
    const V NO_VALUE;
PAD;

public:
    brown_sigouin_int_avl_tm_auto(const K _NO_KEY, const V _NO_VALUE, const int _numThreads);
    ~brown_sigouin_int_avl_tm_auto();

    void initThread(const int tid) {
    }
    void deinitThread(const int tid) {
        TM_NS::thread_registry_deregister_thread();
    }

    V insert(const int tid, const K& key, const V& val) { 
        return doInsert(tid, key, val, false); 
    }

    V insertIfAbsent(const int tid, const K& key, const V& val) { 
        return doInsert(tid, key, val, true); 
    }

    V find(const int tid, const K& key);
    V erase(const int tid, const K &key);
    int rangeQuery(const K& low, const K& hi, K * const resultKeys, void ** const resultValues);

    V guranteedErase(const int tid, const K& key, nodeptr l) {
        nodeptr lleft = l->left;
        nodeptr lright = l->right;
        K lkey = l->key;
        nodeptr p = l->parent;
        nodeptr pleft = p->left;
        nodeptr pright = p->right;
        V retval = l->value;

        // leaf delete
        if (lleft == NULL && lright == NULL) {
            assert(key == lkey);
            if (l == pleft) {
                p->left = NULL;
            } else {
                p->right = NULL;
            }
            // to_retire = l;
            rebalance(tid, p);

        // one child delete
        } else if (lleft == NULL || lright == NULL) {
            assert(key == lkey);
            nodeptr otherNode = lleft ? lleft : lright;
            assert(otherNode && otherNode->key != NO_KEY);
            if (l == p->left) {
                p->left = otherNode;
            } else {
                p->right = otherNode;
            }
            otherNode->parent = p;
            // to_retire = l;
            rebalance(tid, p);

        // two child delete
        } else {
            assert(key == lkey);
            nodeptr succ = doSearchSuccessor(tid, l);
            nodeptr succParent = succ->parent;
            assert(succ && succ->key != NO_KEY);
            assert(succParent && succParent->key != NO_KEY);

            V succvalue = succ->value;
            K succkey = succ->key;

            l->value = succvalue;
            l->key = succkey;

            nodeptr succLeft = succ->left;
            nodeptr succRight = succ->right;
            assert(!succRight || succRight->key != NO_KEY);

            assert(!succLeft);
            if (succParent->left == succ) {
                succParent->left = succRight;
            } else {
                succParent->right = succRight;
            }
            if (succRight) succRight->parent = succParent;
            // to_retire = succ;
            rebalance(tid, succParent);
        }
        // if (to_retire) recmgr->retire(tid, to_retire);
        return retval;
    }

    V guaranteedInsert(const int tid, const K& key, const V& val, nodeptr l) {
        K lkey = l->key;
        nodeptr newLeaf = createNode(tid, l, key, val);
        // to_undo_allocation = newLeaf;
        if (lkey == NO_KEY || cmp(key, lkey)) {
            assert(!l->left);
            l->left = newLeaf;
        } else {
            assert(!l->right);
            l->right = newLeaf;
        }
        rebalance(tid, l);
        return NO_VALUE;
    }

    void update(const int tid, const K& key, const V& val) {
        TM_T::template updateTx<V> ([&] {
            auto l = doSearch(tid, key);
            K lkey = l->key;
            if (key == lkey) {
                guranteedErase(tid, key, l);
            }
            else {
                guaranteedInsert(tid, key, val, l);
            }
            return NO_VALUE;
        });
    }

private:
    int64_t max(int64_t a, int64_t b) {
        if (a >= b) {
            return a;
        }
        return b;
    }

    nodeptr createNode(const int tid, nodeptr parent, K key, V value);
    int64_t getHeight(nodeptr node) {
        if (node == nullptr) {
            return 0;
        }
        else {
            return node->height;
        }
    }

    nodeptr doSearch(const int tid, const K &key);
    nodeptr doSearchSuccessor(const int tid, nodeptr node);
    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    void rebalance(const int tid, nodeptr node);
    bool rotateRight(const int tid, nodeptr parent, nodeptr node, nodeptr left);
    bool rotateLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right);
    bool rotateLeftRight(const int tid, nodeptr parent, nodeptr node, nodeptr left, nodeptr leftRight);
    bool rotateRightLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right, nodeptr rightLeft);

    void freeSubtree(nodeptr node) {
        if (node == NULL) return;
        freeSubtree(node->left);
        freeSubtree(node->right);
        TM_T::tmDelete<Node<K,V>>(node);
    }
};

template <typename K, typename V, class Compare>
brown_sigouin_int_avl_tm_auto<K,V,Compare>::brown_sigouin_int_avl_tm_auto(const K _NO_KEY, const V _NO_VALUE, const int _numThreads)
        : NO_KEY(_NO_KEY), NO_VALUE(_NO_VALUE) {
    cmp = Compare();
    const int tid = 0;
    initThread(tid);

    TM_T::updateTx ([&] {
        root = createNode(tid, NULL, NO_KEY, NULL);
    });

    deinitThread(tid);
}

template <typename K, typename V, class Compare>
brown_sigouin_int_avl_tm_auto<K,V,Compare>::~brown_sigouin_int_avl_tm_auto() {
    int tid = 0;
    initThread(tid);
    TM_T::updateTx ([&] {
        freeSubtree(root);
    });
    deinitThread(tid);
}

template <typename K, typename V, class Compare>
nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare>::createNode(const int tid, nodeptr parent, K key, V value) {
    nodeptr node = TM_T::tmNew<Node<K,V>>();
    node->key = key;
    node->value = value;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
    return node;
}

template <typename K, typename V, class Compare>
nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare>::doSearch(const int tid, const K &key) {
    nodeptr l = root;
    while (true) {
        K currKey = l->key;
        if (currKey == key) return l;
        nodeptr next = nullptr;
        if (currKey == NO_KEY || cmp(key, currKey)) {
            next = l->left;
        }
        else {
            next = l->right;
        }
        if (!next) break;
        l = next;
    }
    return l;
}

template <typename K, typename V, class Compare>
inline nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare>::doSearchSuccessor(const int tid, nodeptr node) {
    nodeptr l = node->right;
    while (l->left) l = l->left;
    return l;
}

template <typename K, typename V, class Compare>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare>::find(const int tid, const K &key) {
    V retval = NO_VALUE;
    TM_T::template readTx<V> ([&] {
        auto l = doSearch(tid, key);
        if (key == l->key) {
            retval = l->value;
        }
        else {
            retval = NO_VALUE;
        }
        return retval;
    });
    return retval;
}

template <typename K, typename V, class Compare>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    V retval = NO_VALUE;

    TM_T::template updateTx<V> ([&] {
        auto l = doSearch(tid, key);
        K lkey = l->key;
        if (key == lkey) {
            retval = l->value;
            if (!onlyIfAbsent) {
                l->value = val;
            }
            return retval;
        } else {
            nodeptr newLeaf = createNode(tid, l, key, val);
            // to_undo_allocation = newLeaf;
            if (lkey == NO_KEY || cmp(key, lkey)) {
                assert(!l->left);
                l->left = newLeaf;
            } else {
                assert(!l->right);
                l->right = newLeaf;
            }
            rebalance(tid, l);
            retval = NO_VALUE;
            return NO_VALUE;
        }
    });
    return retval;
}

template <typename K, typename V, class Compare>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare>::erase(const int tid, const K &key) {
    V retval = NO_VALUE;

    TM_T::template updateTx<V> ([&] {
        auto l = doSearch(tid, key);

        // nodeptr to_retire = NULL;
        nodeptr lleft = l->left;
        nodeptr lright = l->right;
        K lkey = l->key;

        if (key != lkey) {
            retval = NO_VALUE;
            return NO_VALUE;

        // if we find the key
        } else {
            nodeptr p = l->parent;
            nodeptr pleft = p->left;
            nodeptr pright = p->right;
            retval = l->value;

            // leaf delete
            if (lleft == NULL && lright == NULL) {
                assert(key == lkey);
                if (l == pleft) {
                    p->left = NULL;
                } else {
                    p->right = NULL;
                }
                // to_retire = l;
                rebalance(tid, p);

            // one child delete
            } else if (lleft == NULL || lright == NULL) {
                assert(key == lkey);
                nodeptr otherNode = lleft ? lleft : lright;
                assert(otherNode && otherNode->key != NO_KEY);
                if (l == p->left) {
                    p->left = otherNode;
                } else {
                    p->right = otherNode;
                }
                otherNode->parent = p;
                // to_retire = l;
                rebalance(tid, p);

            // two child delete
            } else {
                assert(key == lkey);
                nodeptr succ = doSearchSuccessor(tid, l);
                nodeptr succParent = succ->parent;
                assert(succ && succ->key != NO_KEY);
                assert(succParent && succParent->key != NO_KEY);

                V succvalue = succ->value;
                K succkey = succ->key;

                l->value = succvalue;
                l->key = succkey;

                nodeptr succLeft = succ->left;
                nodeptr succRight = succ->right;
                assert(!succRight || succRight->key != NO_KEY);

                assert(!succLeft);
                if (succParent->left == succ) {
                    succParent->left = succRight;
                } else {
                    succParent->right = succRight;
                }
                if (succRight) succRight->parent = succParent;
                // to_retire = succ;
                rebalance(tid, succParent);
            }
            // if (to_retire) recmgr->retire(tid, to_retire);
            return retval;
        }
    });
    return retval;
}

template <typename K, typename V, class Compare>
void brown_sigouin_int_avl_tm_auto<K,V,Compare>::rebalance(const int tid, nodeptr node) {
    while (node != root) {
        nodeptr parent = node->parent;
        nodeptr left = node->left;
        nodeptr right = node->right;
        int64_t localBalance = getHeight(left) - getHeight(right);

        if (localBalance >= 2) {
            nodeptr leftRight = left->right;
            nodeptr leftLeft = left->left;
            int64_t leftBalance = getHeight(leftLeft) - getHeight(leftRight);

            if (leftBalance < 0) {
                if (!rotateLeftRight(tid, parent, node, left, leftRight)) return;
                node = leftRight;
            } else {
                if (!rotateRight(tid, parent, node, left)) return;
                node = left;
            }
        } else if (localBalance <= -2) {
            nodeptr rightLeft = right->left;
            nodeptr rightRight = right->right;
            int64_t rightBalance = getHeight(rightLeft) - getHeight(rightRight);

            if (rightBalance > 0) {
                if (!rotateRightLeft(tid, parent, node, right, rightLeft)) return;
                node = rightLeft;
            } else {
                if (!rotateLeft(tid, parent, node, right)) return;
                node = right;
            }
        } else {
            int64_t newHeight = max(getHeight(left), getHeight(right)) + 1;
            if (node->height == newHeight) {
                return;
            } else {
                node->height = newHeight;
                node = node->parent;
            }
        }
    }
}

template <typename K, typename V, class Compare>
bool brown_sigouin_int_avl_tm_auto<K,V,Compare>::rotateRight(const int tid, nodeptr parent, nodeptr node, nodeptr left) {
    nodeptr leftRight = left->right;

    if (parent->right == node) {
        parent->right = left;
    } else {
        parent->left = left;
    }

    left->parent = parent;
    left->right = node;
    node->parent = left;
    node->left = leftRight;

    nodeptr nodeleft = node->left;
    nodeptr noderight = node->right;
    nodeptr leftleft = left->left;
    nodeptr leftright = left->right;

    int64_t nodeheight = 1 + max(getHeight(nodeleft), getHeight(noderight));
    node->height = nodeheight;
    int64_t leftheight = 1 + max(getHeight(leftleft), getHeight(leftright));
    left->height = leftheight;

    if (leftRight) leftRight->parent = node;

    nodeptr pleft = parent->left;
    nodeptr pright = parent->right;
    int64_t newHeight = max(getHeight(pleft), getHeight(pright)) + 1;
    if (parent->height == newHeight) {
        return false;
    } else {
        parent->height = newHeight;
        return true;
    }
}

template <typename K, typename V, class Compare>
bool brown_sigouin_int_avl_tm_auto<K,V,Compare>::rotateLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right) {
    nodeptr rightLeft = right->left;

    if (parent->right == node) {
        parent->right = right;
    } else {
        parent->left = right;
    }

    right->parent = parent;
    right->left = node;
    node->parent = right;
    node->right = rightLeft;

    nodeptr nodeleft = node->left;
    nodeptr noderight = node->right;
    nodeptr rightleft = right->left;
    nodeptr rightright = right->right;

    int64_t nodeheight = 1 + max(getHeight(nodeleft), getHeight(noderight));
    node->height = nodeheight;
    int64_t rightheight = 1 + max(getHeight(rightleft), getHeight(rightright));
    right->height = rightheight;

    if (rightLeft) rightLeft->parent = node;

    nodeptr pleft = parent->left;
    nodeptr pright = parent->right;
    int64_t newHeight = max(getHeight(pleft), getHeight(pright)) + 1;
    if (parent->height == newHeight) {
        return false;
    } else {
        parent->height = newHeight;
        return true;
    }
}

template <typename K, typename V, class Compare>
inline bool brown_sigouin_int_avl_tm_auto<K,V,Compare>::rotateLeftRight(const int tid, nodeptr parent, nodeptr node, nodeptr left, nodeptr leftRight) {
    nodeptr pleft = parent->left;
    nodeptr pright = parent->right;
    int64_t oldHeight = max(getHeight(pleft), getHeight(pright)) + 1;

    rotateLeft(tid, node, left, leftRight);
    rotateRight(tid, parent, node, leftRight);

    nodeptr pleft2 = parent->left;
    nodeptr pright2 = parent->right;
    int64_t newHeight = max(getHeight(pleft2), getHeight(pright2)) + 1;
    return (newHeight != oldHeight);
}

template <typename K, typename V, class Compare>
inline bool brown_sigouin_int_avl_tm_auto<K,V,Compare>::rotateRightLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right, nodeptr rightLeft) {
    nodeptr pleft = parent->left;
    nodeptr pright = parent->right;
    int64_t oldHeight = max(getHeight(pleft), getHeight(pright)) + 1;

    rotateRight(tid, node, right, rightLeft);
    rotateLeft(tid, parent, node, rightLeft);

    nodeptr pleft2 = parent->left;
    nodeptr pright2 = parent->right;
    int64_t newHeight = max(getHeight(pleft2), getHeight(pright2)) + 1;
    return (newHeight != oldHeight);
}

template<class K, class V, class Compare>
int brown_sigouin_int_avl_tm_auto<K,V,Compare>::rangeQuery(const K& low, const K& hi, K * const resultKeys, void ** const resultValues) {
    int result = 0;
    std::vector<nodeptr> stack;

    // depth first traversal (of interesting subtrees)
    TM_T::readTx ([&] {
        stack.clear();
        result = 0;

        stack.push_back(root);
        while (!stack.empty()) {
            nodeptr node = stack.back();
            stack.pop_back();
            nodeptr left = node->left;
            nodeptr right = node->right;
            K key = node->key;

            // check if we should update this node's value
            if (key != this->NO_KEY && !cmp(key, low) && !cmp(hi, key)) {
                V v = node->value;
                // node->value = (V) ((uintptr_t) v + 1);
                resultKeys[result] = key;
                resultValues[result] = v;
                ++result;
            }

            // explore this node's children if appropriate
            if (right && (key != this->NO_KEY && !cmp(hi, key))) {
                stack.push_back(right);
            }
            if (left && (key == this->NO_KEY || cmp(low, key))) {
                stack.push_back(left);
            }
        }
    });
    return result;
}