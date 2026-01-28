#pragma once

#include <cassert>
#include <ctime>
#include <fstream>
#include <immintrin.h>
#include <iomanip>
#include <iostream>
#include <unordered_set>

#include "tm.h"
#include "record_manager.h"

using namespace std;

#ifndef MAX_THREADS_POW2
#define MAX_THREADS_POW2 512
#endif
#ifndef PAD
#define CAT2(x, y) x##y
#define CAT(x, y) CAT2(x, y)
#define PAD volatile char CAT(___padding, __COUNTER__)[128]
#endif

template <typename K, typename V, tx_safety mode = SAFE>
class Node {
  public:
    tx_field<mode, K                > key;
    tx_field<mode, Node<K,V,mode> * > left;
    tx_field<mode, Node<K,V,mode> * > right;
    tx_field<mode, Node<K,V,mode> * > parent;
    tx_field<mode, int64_t          > height;
    tx_field<mode, V                > value;
};
#define nodeptr Node<K,V> *
#define nodeptr_unsafe Node<K,V,UNSAFE> *

// thread_local void * to_undo_allocation = NULL;

template <typename K, typename V, class Compare, class RecordMgr>
class brown_sigouin_int_avl_tm_auto {
private:
PAD;
    RecordMgr * const recmgr;
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
        recmgr->initThread(tid);
        TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }

    V find(const int tid, const K& key);
    V insert(const int tid, const K& key, const V& val) { return doInsert(tid, key, val, false); }
    V insertIfAbsent(const int tid, const K& key, const V& val) { return doInsert(tid, key, val, true); }
    V erase(const int tid, const K &key);
    int rangeUpdate(const int tid, const K& low, const K& hi);
    void printDebuggingDetails();
    bool validate();
    nodeptr_unsafe debug_getEntryPoint() { return (nodeptr_unsafe) root; }


    int rangeQuery(const int tid, const K& low, const K& hi, K * const resultKeys, void ** const resultValues) {
        auto guard = recmgr->getGuard(tid);
        int result = 0;
        std::vector<nodeptr> stack;

        // depth first traversal (of interesting subtrees)
        TM_BEGIN_RO();
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
        TM_END();
        return result;
    }

    V guranteedErase(const int tid, const K& key, nodeptr l) {
        nodeptr lleft = l->left;
        nodeptr lright = l->right;
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
        auto guard = recmgr->getGuard(tid);
        TM_BEGIN();
            auto l = doSearch(tid, key);
            K lkey = l->key;
            if (key == lkey) {
                guranteedErase(tid, key, l);
            }
            else {
                guaranteedInsert(tid, key, val, l);
            }
        TM_END();
    }

private:
    nodeptr createNode(const int tid, nodeptr parent, K key, V value);
    int64_t getHeight(nodeptr node) {
        return node == NULL ? 0 : node->height;
    }

    nodeptr doSearch(const int tid, const K &key);
    nodeptr doSearchSuccessor(const int tid, nodeptr node);
    V doInsert(const int tid, const K& key, const V& value, bool onlyIfAbsent);
    void rebalance(const int tid, nodeptr node);
    bool rotateRight(const int tid, nodeptr parent, nodeptr node, nodeptr left);
    bool rotateLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right);
    bool rotateLeftRight(const int tid, nodeptr parent, nodeptr node, nodeptr left, nodeptr leftRight);
    bool rotateRightLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right, nodeptr rightLeft);

    void freeSubtree(nodeptr_unsafe node) {
        if (node == NULL) return;
        freeSubtree(node->left);
        freeSubtree(node->right);
        const int dummy_tid = 0;
        recmgr->deallocate(dummy_tid, node);
    }
    long validateSubtree(nodeptr_unsafe node, long smaller, long larger, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound);
};

template <typename K, typename V, class Compare, class RecordMgr>
brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::brown_sigouin_int_avl_tm_auto(const K _NO_KEY, const V _NO_VALUE, const int _numThreads)
        : recmgr(new RecordMgr(_numThreads))
        , NO_KEY(_NO_KEY)
        , NO_VALUE(_NO_VALUE) {

    VERBOSE DEBUG COUTATOMIC("constructor brown_sigouin_int_avl_tm_auto"<<std::endl);
    cmp = Compare();
    const int tid = 0;
    initThread(tid);

    assert(_numThreads < MAX_THREADS_POW2);
    TM_BEGIN();
    root = createNode(tid, NULL, NO_KEY, NULL);
    TM_END();
}

template <typename K, typename V, class Compare, class RecordMgr>
brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::~brown_sigouin_int_avl_tm_auto() {
    VERBOSE DEBUG COUTATOMIC("destructor brown_sigouin_int_avl_tm_auto");
    int tid = 0;
    initThread(tid);
    freeSubtree((nodeptr_unsafe) root);
    recmgr->printStatus();
    deinitThread(tid);
    delete recmgr;
}

template <typename K, typename V, class Compare, class RecordMgr>
nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::createNode(const int tid, nodeptr parent, K key, V value) {
    nodeptr node = recmgr->template allocate<Node<K,V>>(tid);
    if (node == NULL) setbench_error("could not allocate node");
    node->key = key;
    node->value = value;
    node->parent = parent;
    node->left = NULL;
    node->right = NULL;
    node->height = 1;
    // node->key.store_unsafe(key);
    // node->value.store_unsafe(value);
    // node->parent.store_unsafe(parent);
    // node->left.store_unsafe(NULL);
    // node->right.store_unsafe(NULL);
    // node->height.store_unsafe(1);
    return node;
}

template <typename K, typename V, class Compare, class RecordMgr>
nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::doSearch(const int tid, const K &key) {
    nodeptr l = root;
    while (true) {
        K currKey = l->key;
        if (currKey == key) return l;
        nodeptr next = (currKey == NO_KEY || cmp(key, currKey)) ? l->left : l->right;
        if (!next) break;
        l = next;
    }
    return l;
}

template <typename K, typename V, class Compare, class RecordMgr>
inline nodeptr brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::doSearchSuccessor(const int tid, nodeptr node) {
    nodeptr l = node->right;
    while (l->left) l = l->left;
    return l;
}

template <typename K, typename V, class Compare, class RecordMgr>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::find(const int tid, const K &key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN_RO();
    auto l = doSearch(tid, key);
    V retval = (key == l->key) ? l->value : NO_VALUE;
    TM_END();
    return retval;
}

template <typename K, typename V, class Compare, class RecordMgr>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    auto guard = recmgr->getGuard(tid);

    // to_undo_allocation = NULL;
    TM_BEGIN();
    // if (to_undo_allocation) {
    //     recmgr->deallocate(tid, (nodeptr) to_undo_allocation);
    //     to_undo_allocation = NULL;
    // }

    auto l = doSearch(tid, key);
    K lkey = l->key;
    if (key == lkey) {
        V result = l->value;
        assert(result != NO_VALUE);
        if (!onlyIfAbsent) {
            l->value = val;
        }
        TM_END();
        return result;
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
        TM_END();
        return NO_VALUE;
    }
    assert(false);
}

template <typename K, typename V, class Compare, class RecordMgr>
inline V brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::erase(const int tid, const K &key) {
    auto guard = recmgr->getGuard(tid);

    TM_BEGIN();
    auto l = doSearch(tid, key);

    // nodeptr to_retire = NULL;
    nodeptr lleft = l->left;
    nodeptr lright = l->right;
    K lkey = l->key;

    if (key != lkey) {
        TM_END();
        return NO_VALUE;

    // if we find the key
    } else {
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

        TM_END();
        // if (to_retire) recmgr->retire(tid, to_retire);
        return retval;
    }
    assert(false);
}

template <typename K, typename V, class Compare, class RecordMgr>
void brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::rebalance(const int tid, nodeptr node) {
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

template <typename K, typename V, class Compare, class RecordMgr>
bool brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::rotateRight(const int tid, nodeptr parent, nodeptr node, nodeptr left) {
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

template <typename K, typename V, class Compare, class RecordMgr>
bool brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::rotateLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right) {
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

template <typename K, typename V, class Compare, class RecordMgr>
inline bool brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::rotateLeftRight(const int tid, nodeptr parent, nodeptr node, nodeptr left, nodeptr leftRight) {
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

template <typename K, typename V, class Compare, class RecordMgr>
inline bool brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::rotateRightLeft(const int tid, nodeptr parent, nodeptr node, nodeptr right, nodeptr rightLeft) {
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

template<class K, class V, class Compare, class RecManager>
int brown_sigouin_int_avl_tm_auto<K,V,Compare,RecManager>::rangeUpdate(const int tid, const K& low, const K& hi) {
    auto guard = recmgr->getGuard(tid);
    int result = 0;
    block<Node<K,V,SAFE> > stack (NULL);

    // depth first traversal (of interesting subtrees)
    TM_BEGIN();
    stack.clearWithoutFreeingElements();
    stack.push(root);
    while (!stack.isEmpty()) {
        nodeptr node = stack.pop();
        nodeptr left = node->left;
        nodeptr right = node->right;
        K key = node->key;

        // check if we should update this node's value
        if (key != this->NO_KEY && !cmp(key, low) && !cmp(hi, key)) {
            V v = node->value;
            node->value = (V) ((uintptr_t) v + 1);
            ++result;
        }

        // explore this node's children if appropriate
        if (right && (key != this->NO_KEY && !cmp(hi, key))) {
            stack.push(right);
        }
        if (left && (key == this->NO_KEY || cmp(low, key))) {
            stack.push(left);
        }
    }
    TM_END();
    return result;
}

template <typename K, typename V, class Compare, class RecordMgr>
void brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::printDebuggingDetails() {
}

template <typename K, typename V, class Compare, class RecordMgr>
long brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::validateSubtree(nodeptr_unsafe node, long smaller, long larger, std::unordered_set<K> &keys, ofstream &graph, ofstream &log, bool &errorFound) {
    // if (node == NULL)
    //     return 0;
    // graph << "\"" << node << "\""
    //       << "[label=\"K: " << node->key << " - H: "
    //       << node->height << "\"];\n";

    // nodeptr_unsafe nodeLeft = node->left;
    // nodeptr_unsafe nodeRight = node->right;

    // if (nodeLeft != NULL) {
    //     graph << "\"" << node << "\" -> \"" << nodeLeft << "\"";
    //     if (node->key < nodeLeft->key) {
    //         log << "BST violation! " << node->key << "\n";
    //         errorFound = true;
    //         graph << "[color=red]";
    //     } else {
    //         graph << "[color=blue]";
    //     }

    //     graph << ";\n";
    // }

    // if (nodeRight != NULL) {
    //     graph << "\"" << node << "\" -> \"" << nodeRight << "\"";
    //     if (node->key > nodeRight->key) {
    //         log << "BST violation! " << node->key << "\n";
    //         errorFound = true;
    //         graph << "[color=red]";
    //     } else {
    //         graph << "[color=green]";
    //     }
    //     graph << ";\n";
    // }

    // nodeptr_unsafe parent = node->parent;
    // graph << "\"" << node << "\" -> \"" << parent << "\""
    //                                                  "[color=grey];\n";
    // int64_t height = node->height;

    // if (!(keys.count(node->key) == 0)) {
    //     log << "DUPLICATE KEY! " << node->key << "\n";
    //     errorFound = true;
    // }

    // if (!((nodeLeft == NULL || nodeLeft->parent == node) &&
    //       (nodeRight == NULL || nodeRight->parent == node))) {
    //     log << "IMPROPER PARENT! " << node->key << "\n";
    //     errorFound = true;
    // }

    // if ((node->key < smaller) || (node->key > larger)) {
    //     log << "IMPROPER LOCAL TREE! " << node->key << "\n";
    //     errorFound = true;
    // }

    // if (nodeLeft == NULL && nodeRight == NULL && (node == NULL ? 0 : node->height) > 1) {
    //     log << "Leaf with height > 1! " << node->key << "\n";
    //     errorFound = true;
    // }

    // keys.insert(node->key);

    // long lHeight = validateSubtree(node->left, smaller, node->key, keys, graph, log, errorFound);
    // long rHeight = validateSubtree(node->right, node->key, larger, keys, graph, log, errorFound);

    // long ret = 1 + max(lHeight, rHeight);

    // if (node->height != ret) {
    //     log << "Node " << node->key << " with height " << ret << " thinks it has height " << node->height << "\n";
    //     errorFound = true;
    // }

    // if (abs(lHeight - rHeight) > 1) {
    //     log << "Imbalanced Node! " << node->key << "(" << lHeight << ", " << rHeight << ") - " << node->height << "\n";
    //     errorFound = true;
    // }

    // return ret;
    return 1;
}

template <typename K, typename V, class Compare, class RecordMgr>
bool brown_sigouin_int_avl_tm_auto<K,V,Compare,RecordMgr>::validate() {
    // std::unordered_set<K> keys = {};
    // bool errorFound;

    // rename("graph.dot", "graph_before.dot");
    // ofstream graph;
    // graph.open("graph.dot");
    // graph << "digraph G {\n";

    // ofstream log;
    // log.open("log.txt", std::ofstream::out);

    // auto t = std::time(nullptr);
    // auto tm = *(std::localtime(&t));
    // log << "Run at: " << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << "\n";

    // const K MINKEY = (K) 0;
    // long ret = validateSubtree(debug_getEntryPoint()->left, MINKEY, MAXKEY, keys, graph, log, errorFound);
    // graph << "}";
    // graph.close();

    // if (!errorFound) {
    //     log << "Validated Successfully!\n";
    // }

    // log.close();

    // return !errorFound;
    return true;
}
