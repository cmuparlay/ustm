#pragma once

#include <cassert>

#define MAX_KCAS 21
#include "kcas.h"

#include <ctime>
#include <fstream>
#include <immintrin.h>
#include <iomanip>
#include <iostream>
#include <unordered_set>

using namespace std;

#ifndef MAX_THREADS_POW2
#define MAX_THREADS_POW2 512
#endif
#define PADDING_BYTES 128

#define IS_MARKED(word) (word & 0x1)

template <typename K, typename V>
struct Node {
    casword<casword_t> vNumMark;
    casword<K> key;
    casword<Node<K, V> *> left;
    casword<Node<K, V> *> right;
    casword<Node<K, V> *> parent;
    casword<int> height;
    casword<V> value;
};

enum RetCode : int {
    RETRY = 0,
    UNNECCESSARY = 0,
    FAILURE = -1,
    SUCCESS = 1,
    SUCCESS_WITH_HEIGHT_UPDATE = 2
};

template <class RecordManager, typename K, typename V>
class InternalKCAS {
  private:
    volatile char padding0[PADDING_BYTES];
    //Debugging, used to validate that no thread's parent can't be NULL, save for the root
    bool init = false;
    const int numThreads;
    const int minKey;
    const long long maxKey;
    volatile char padding4[PADDING_BYTES];
    Node<K, V> *root;
    volatile char padding5[PADDING_BYTES];
    RecordManager *const recmgr;
    volatile char padding7[PADDING_BYTES];

  public:
    InternalKCAS(const int _numThreads, const int _minKey, const long long _maxKey);
    ~InternalKCAS();
    bool contains(const int tid, const K &key);
    V insertIfAbsent(const int tid, const K &key, const V &value);
    V erase(const int tid, const K &key);
    bool validate();
    void printDebuggingDetails();
    Node<K, V> *getRoot();
    void initThread(const int tid);
    void deinitThread(const int tid);
    int getHeight(Node<K, V> *node);

  private:
    Node<K, V> *createNode(const int tid, Node<K, V> *parent, K key, V value);
    void freeSubtree(const int tid, Node<K, V> *node);
    long validateSubtree(Node<K, V> *node, long smaller, long larger, std::unordered_set<casword_t> &keys, ofstream &graph, ofstream &log, bool &errorFound);
    int internalErase(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, const K &key);
    int internalInsert(const int tid, Node<K, V> *parent, casword_t pVer, const K &key, const V &value);
    int countChildren(const int tid, Node<K, V> *node);
    int getSuccessor(const int tid, Node<K, V> *node, Node<K, V> *&succParent, casword_t &spVer, Node<K, V> *&succ, casword_t &sVer);
    int search(const int tid, const K &key);
    int search(const int tid, Node<K, V> *&parent, casword_t &pVer, Node<K, V> *&node, casword_t &nVer, const K &key);
    int rotateRight(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *leftRight, casword_t lrVer, Node<K, V> *leftLeft, casword_t llVer);
    int rotateLeft(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *rightLeft, casword_t rlVer, Node<K, V> *rightRight, casword_t rrVer);
    int rotateLeftRight(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *leftRight, casword_t lrVer, Node<K, V> *leftLeft, casword_t llVer);
    int rotateRightLeft(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *rightLeft, casword_t rlVer, Node<K, V> *rightRight, casword_t rrVer);
    void fixHeightAndRebalance(const int tid, Node<K, V> *node);
    int fixHeight(const int tid, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer);
};

template <class RecordManager, typename K, typename V>
Node<K, V> *InternalKCAS<RecordManager, K, V>::createNode(const int tid, Node<K, V> *parent, K key, V value) {
    Node<K, V> *node = recmgr->template allocate<Node<K, V>>(tid);
    //No node, save for root, should have a NULL parent
    assert(!init || parent->key < maxKey);
    node->key.setInitVal(key);
    node->value.setInitVal(value);
    node->parent.setInitVal(parent);
    node->vNumMark.setInitVal(0);
    node->left.setInitVal(NULL);
    node->right.setInitVal(NULL);
    node->height.setInitVal(1);
    return node;
}

template <class RecordManager, typename K, typename V>
InternalKCAS<RecordManager, K, V>::InternalKCAS(const int _numThreads, const int _minKey, const long long _maxKey)
    : numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey), recmgr(new RecordManager(numThreads)) {
    assert(_numThreads < MAX_THREADS_POW2);
    int tid = 0;
    initThread(tid);
    root = createNode(tid, NULL, (maxKey + 1) & 0x00FFFFFFFFFFFFFF, NULL);
    init = true;
}

template <class RecordManager, typename K, typename V>
InternalKCAS<RecordManager, K, V>::~InternalKCAS() {
    int tid = 0;
    initThread(tid);
    freeSubtree(tid, root);
    deinitThread(tid);
    delete recmgr;
}

template <class RecordManager, typename K, typename V>
inline Node<K, V> *InternalKCAS<RecordManager, K, V>::getRoot() {
    return root->left;
}

template <class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::initThread(const int tid) {
    recmgr->initThread(tid);
}

template <class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::getHeight(Node<K, V> *node) {
    return node == NULL ? 0 : node->height;
}

template <class RecordManager, typename K, typename V>
inline int InternalKCAS<RecordManager, K, V>::getSuccessor(const int tid, Node<K, V> *node, Node<K, V> *&succParent, casword_t &spVer, Node<K, V> *&succ, casword_t &sVer) {
    succParent = NULL;
    succ = node;
    Node<K, V> *next = node->right;

    while (next != NULL) {
        succParent = succ;
        spVer = sVer;
        succ = next;
        sVer = kcas::visit(succ);
        next = next->left;
    }
    return RetCode::SUCCESS;
}

template <class RecordManager, typename K, typename V>
inline bool InternalKCAS<RecordManager, K, V>::contains(const int tid, const K &key) {
    assert(key <= maxKey);
    auto guard = recmgr->getGuard(tid, true);
    while (true) {
        kcas::start();
        auto res = search(tid, key);
        if (res == RetCode::SUCCESS)
            return true;
        else if (kcas::validate())
        return false;
    }
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::search(const int tid, const K &key) {
    assert(key <= maxKey);

    K currKey;

    kcas::visit(root);

    Node<K, V> *node = root->left;

    while (true) {
        //We have hit a terminal node without finding our key, must validate
        if (node == NULL) {
            return RetCode::FAILURE;
        }
        kcas::visit(node);
        currKey = node->key;

        if (key > currKey) {
            node = node->right;
        } else if (key < currKey) {
            node = node->left;
        } //no validation required on finding a key
        else {
            return RetCode::SUCCESS;
        }
    }
}

// template <class RecordManager, typename K, typename V>
// int InternalKCAS<RecordManager, K, V>::rangeQuery(const int tid, K low, k high)
// {
//         kcas::start();
//         while(true){
//                 long long count = 0;
//         }
// }

// template <class RecordManager, typename K, typename V>
// int InternalKCAS<RecordManager, K, V>::doRangeQuery(const int tid, Node<K, V> * curr, K low, k high, long long &count)
// {

// }

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::search(const int tid, Node<K, V> *&parent, casword_t &pVer, Node<K, V> *&node, casword_t &nVer, const K &key) {
    assert(key <= maxKey);

    K currKey;
    casword_t tempVer = -1;
    pVer = kcas::visit(root);
    parent = root;
    node = root->left;

    while (true) {
        //We have hit a terminal node without finding our key, must validate
        if (node == NULL) {
            return RetCode::FAILURE;
        }

        nVer = kcas::visit(node);
        currKey = node->key;

        if (key > currKey) {
            pVer = nVer;
            parent = node;
            node = node->right;
        } else if (key < currKey) {
            pVer = nVer;
            parent = node;
            node = node->left;
        } //no validation required on finding a key
        else {
            return RetCode::SUCCESS;
        }
    }
}

template <class RecordManager, typename K, typename V>
inline V InternalKCAS<RecordManager, K, V>::insertIfAbsent(const int tid, const K &key, const V &value) {
    Node<K, V> *node;
    Node<K, V> *parent;
    casword_t nVer;
    casword_t pVer;

    while (true) {
        kcas::start();
        auto guard = recmgr->getGuard(tid);

        int res = search(tid, parent, pVer, node, nVer, key);

        if (res == RetCode::SUCCESS) {
            return (V)node->value;
        }

        assert(res == RetCode::FAILURE);
        if (internalInsert(tid, parent, pVer, key, value) == RetCode::SUCCESS) {
            return 0;
        }
    }
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::internalInsert(const int tid, Node<K, V> *parent, casword_t pVer, const K &key, const V &value) {

    Node<K, V> *newNode = createNode(tid, parent, key, value);

    if (key > parent->key) {
        kcas::add(&parent->right, (Node<K, V> *)NULL, newNode);
    } else if (key < parent->key) {
        kcas::add(&parent->left, (Node<K, V> *)NULL, newNode);
    } else {
        recmgr->deallocate(tid, newNode);
        return RetCode::RETRY;
    }

    kcas::add(&parent->vNumMark, pVer, pVer + 2);
    if (kcas::validateAndExecute()) {
        fixHeightAndRebalance(tid, parent);
        return RetCode::SUCCESS;
    }

    recmgr->deallocate(tid, newNode);
    return RetCode::RETRY;
}

template <class RecordManager, typename K, typename V>
inline V InternalKCAS<RecordManager, K, V>::erase(const int tid, const K &key) {
    Node<K, V> *node;
    Node<K, V> *parent;
    casword_t nVer;
    casword_t pVer;

    while (true) {
        kcas::start();
        auto guard = recmgr->getGuard(tid);

        int res = search(tid, parent, pVer, node, nVer, key);

        if (res == RetCode::FAILURE) {
            if (kcas::validate())
                return 0;
            continue;
        }

        assert(res == RetCode::SUCCESS);
        res = internalErase(tid, parent, pVer, node, nVer, key);
        if (res == RetCode::SUCCESS) {
            return (V)node->value;
        } else if (res == RetCode::FAILURE) {
            return 0;
        }
    }
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::internalErase(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, const K &key) {
    int numChildren = countChildren(tid, node);

    if (IS_MARKED(pVer) || IS_MARKED(nVer)) {
        return RetCode::RETRY;
    }

    if (numChildren == 0) {
        /* No-Child Delete
         * Unlink node
         */

        if (key > parent->key) {
            kcas::add(&parent->right, node, (Node<K, V> *)NULL);
        } else if (key < parent->key) {
            kcas::add(&parent->left, node, (Node<K, V> *)NULL);
        } else {
            return RetCode::RETRY;
        }

        kcas::add(
            &parent->vNumMark, pVer, pVer + 2,
            &node->vNumMark, nVer, nVer + 3);

        if (kcas::execute()) {
            assert(IS_MARKED(node->vNumMark));
            recmgr->retire(tid, node);

            fixHeightAndRebalance(tid, parent);

            return RetCode::SUCCESS;
        }

        return RetCode::RETRY;
    } else if (numChildren == 1) {
        /* One-Child Delete
         * Reroute parent pointer around removed node
         */

        Node<K, V> *left = node->left;
        Node<K, V> *right = node->right;
        Node<K, V> *reroute;

        //determine which child will be the replacement
        if (left != NULL) {
            reroute = left;
        } else if (right != NULL) {
            reroute = right;
        } else {
            return RetCode::RETRY;
        }

        casword_t rVer = kcas::visit(reroute);

        if (IS_MARKED(rVer)) {
            return RetCode::RETRY;
        }

        if (key > parent->key) {
            kcas::add(&parent->right, node, reroute);
        } else if (key < parent->key) {
            kcas::add(&parent->left, node, reroute);
        } else {
            return RetCode::RETRY;
        }

        kcas::add(
            &reroute->parent, node, parent,
            &reroute->vNumMark, rVer, rVer + 2,
            &node->vNumMark, nVer, nVer + 3,
            &parent->vNumMark, pVer, pVer + 2);

        if (kcas::execute()) {
            assert(IS_MARKED(node->vNumMark));
            recmgr->retire(tid, node);
            fixHeightAndRebalance(tid, parent);

            return RetCode::SUCCESS;
        }

        return RetCode::RETRY;
    } else if (numChildren == 2) {
        /* Two-Child Delete
         * Promotion of descendant successor to this node by replacing the key/value pair at the node
         */
        Node<K, V> *succ;
        casword_t sVer = nVer;
        Node<K, V> *succParent;
        casword_t spVer = -1;
        //the (decendant) successor's key will be promoted
        getSuccessor(tid, node, succParent, spVer, succ, sVer);

        if (node == succ) //no longer has two children
        {
            return RetCode::RETRY;
        }
        assert(sVer != -1);

        if (succParent == NULL) {
            return RetCode::RETRY;
        }

        K succKey = succ->key;

        assert(succKey <= maxKey);

        if (IS_MARKED(spVer)) {
            return RetCode::RETRY;
        }

        Node<K, V> *succRight = succ->right;

        if (succRight != NULL) {
            casword_t srVer = kcas::visit(succRight);

            if (IS_MARKED(srVer)) {
                return RetCode::RETRY;
            }

            kcas::add(
                &succRight->parent, succ, succParent,
                &succRight->vNumMark, srVer, srVer + 2);
        }

        if (succParent->right == succ) {
            kcas::add(&succParent->right, succ, succRight);
        } else if (succParent->left == succ) {
            kcas::add(&succParent->left, succ, succRight);
        } else {
            return RetCode::RETRY;
        }

        V nodeVal = node->value;
        V succVal = succ->value;

        kcas::add(
            &node->value, nodeVal, succVal,
            &node->key, key, succKey,
            &succ->vNumMark, sVer, sVer + 3,
            &node->vNumMark, nVer, nVer + 2);

        if (succParent != node) {
            kcas::add(&succParent->vNumMark, spVer, spVer + 2);
        }

        if (kcas::validateAndExecute()) {
            assert(IS_MARKED(succ->vNumMark));
            recmgr->retire(tid, succ);
            //successor's parent is the only node that's height will have been impacted
            fixHeightAndRebalance(tid, succParent);
            return RetCode::SUCCESS;
        }
        return RetCode::RETRY;
    }
    assert(false);
    return RetCode::RETRY;
}

template <class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::fixHeightAndRebalance(const int tid, Node<K, V> *node) {
    int propRes;

    while (node != root) {
        kcas::start();
        Node<K, V> *parent, *right, *left, *rightLeft, *leftRight, *rightRight, *leftLeft;
        casword_t nVer, pVer, rVer, lVer, rlVer, lrVer, rrVer, llVer;

        nVer = kcas::visit(node);
        parent = node->parent;

        pVer = kcas::visit(parent);

        if (IS_MARKED(nVer)) {
            return;
        }

        left = node->left;
        if (left != NULL) {
            lVer = kcas::visit(left);
        }

        right = node->right;
        if (right != NULL) {
            rVer = kcas::visit(right);
        }

        int localBalance = getHeight(left) - getHeight(right);

        if (localBalance >= 2) {
            if (left == NULL || IS_MARKED(lVer)) {
                continue;
            }

            leftRight = left->right;
            leftLeft = left->left;

            if (leftRight != NULL) {
                lrVer = kcas::visit(leftRight);
            }

            if (leftLeft != NULL) {
                llVer = kcas::visit(leftLeft);
            }

            int leftBalance = getHeight(leftLeft) - getHeight(leftRight);

            if (leftBalance < 0) {
                if (leftRight == NULL) {
                    continue;
                }
                if (rotateLeftRight(tid, parent, pVer, node, nVer, left, lVer, right, rVer, leftRight, lrVer, leftLeft, llVer) == RetCode::SUCCESS) {
                    //node is now the lowest on the tree, so it must be rebalanced first cannot simply loop...
                    fixHeightAndRebalance(tid, node);
                    fixHeightAndRebalance(tid, left);
                    fixHeightAndRebalance(tid, leftRight);
                    node = parent;
                }
            } else {
                if (rotateRight(tid, parent, pVer, node, nVer, left, lVer, right, rVer, leftRight, lrVer, leftLeft, llVer) == RetCode::SUCCESS) {
                    fixHeightAndRebalance(tid, node);
                    fixHeightAndRebalance(tid, left);
                    node = parent;
                }
            }
        } else if (localBalance <= -2) {
            if (right == NULL || IS_MARKED(rVer)) {
                continue;
            }

            rightLeft = right->left;
            rightRight = right->right;

            if (rightLeft != NULL) {
                rlVer = kcas::visit(rightLeft);
            }

            if (rightRight != NULL) {
                rrVer = kcas::visit(rightRight);
            }

            int rightBalance = getHeight(rightLeft) - getHeight(rightRight);

            if (rightBalance > 0) {
                if (rightLeft == NULL) {
                    continue;
                }

                if (rotateRightLeft(tid, parent, pVer, node, nVer, left, lVer, right, rVer, rightLeft, rlVer, rightRight, rrVer) == RetCode::SUCCESS) {
                    fixHeightAndRebalance(tid, node);
                    fixHeightAndRebalance(tid, right);
                    fixHeightAndRebalance(tid, rightLeft);
                    node = parent;
                }
            } else {
                if (rotateLeft(tid, parent, pVer, node, nVer, left, lVer, right, rVer, rightLeft, rlVer, rightRight, rrVer) == RetCode::SUCCESS) {
                    fixHeightAndRebalance(tid, node);
                    fixHeightAndRebalance(tid, right);
                    node = parent;
                }
            }
        } else {
            //no rebalance occurred? check if the height is still ok
            if ((propRes = fixHeight(tid, node, nVer, left, lVer, right, rVer)) == RetCode::FAILURE) {
                continue;
            } else if (propRes == RetCode::SUCCESS_WITH_HEIGHT_UPDATE) {
                node = node->parent;
            } else {
                return;
            }
        }
    }
    return;
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::fixHeight(const int tid, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer) {

    if (left != NULL) {
        kcas::add(&left->vNumMark, lVer, lVer);
    }

    if (right != NULL) {
        kcas::add(&right->vNumMark, rVer, rVer);
    }

    int oldHeight = node->height;

    int newHeight = 1 + max(getHeight(left), getHeight(right));

    //Check if rebalance is actually necessary
    if (oldHeight == newHeight) {
        if (node->vNumMark == nVer && (left == NULL || left->vNumMark == lVer) && (right == NULL || right->vNumMark == rVer)) {
            return RetCode::UNNECCESSARY;
        } else {
            return RetCode::FAILURE;
        }
    }

    kcas::add(
        &node->height, oldHeight, newHeight,
        &node->vNumMark, nVer, nVer + 2);

    if (kcas::execute()) {
        return RetCode::SUCCESS_WITH_HEIGHT_UPDATE;
    }

    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::rotateRight(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *leftRight, casword_t lrVer, Node<K, V> *leftLeft, casword_t llVer) {
    /***Pointers to Parents and Children***/
    //could fail fast here, should consider
    if (parent->right == node) {
        kcas::add(&parent->right, node, left);
    } else if (parent->left == node) {
        kcas::add(&parent->left, node, left);
    } else {
        return RetCode::FAILURE;
    }

    if (leftRight != NULL) {
        kcas::add(
            &leftRight->parent, left, node,
            &leftRight->vNumMark, lrVer, lrVer + 2);
    }

    if (leftLeft != NULL) {
        kcas::add(&leftLeft->vNumMark, llVer, llVer);
    }

    if (right != NULL) {
        kcas::add(&right->vNumMark, rVer, rVer);
    }

    int oldNodeHeight = node->height;
    int oldLeftHeight = left->height;

    int newNodeHeight = 1 + max(getHeight(leftRight), getHeight(right));
    int newLeftHeight = 1 + max(getHeight(leftLeft), newNodeHeight);

    kcas::add(
        &left->parent, node, parent,
        &node->left, left, leftRight,
        &left->right, leftRight, node,
        &node->parent, parent, left,
        &node->height, oldNodeHeight, newNodeHeight,
        &left->height, oldLeftHeight, newLeftHeight,
        &parent->vNumMark, pVer, pVer + 2,
        &node->vNumMark, nVer, nVer + 2,
        &left->vNumMark, lVer, lVer + 2);

    if (kcas::execute())
        return RetCode::SUCCESS;
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::rotateLeft(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *rightLeft, casword_t rlVer, Node<K, V> *rightRight, casword_t rrVer) {
    /***Pointers to Parents and Children***/
    //could fail fast here, should consider
    if (parent->right == node) {
        kcas::add(&parent->right, node, right);
    } else if (parent->left == node) {
        kcas::add(&parent->left, node, right);
    } else {
        return RetCode::FAILURE;
    }

    if (rightLeft != NULL) {
        kcas::add(
            &rightLeft->parent, right, node,
            &rightLeft->vNumMark, rlVer, rlVer + 2);
    }

    if (rightRight != NULL) {
        kcas::add(&rightRight->vNumMark, rrVer, rrVer);
    }

    if (left != NULL) {
        kcas::add(&left->vNumMark, lVer, lVer);
    }

    int oldNodeHeight = node->height;
    int oldRightHeight = right->height;

    int newNodeHeight = 1 + max(getHeight(left), getHeight(rightLeft));
    int newRightHeight = 1 + max(newNodeHeight, getHeight(rightRight));

    kcas::add(
        &right->parent, node, parent,
        &node->right, right, rightLeft,
        &right->left, rightLeft, node,
        &node->parent, parent, right,
        &node->height, oldNodeHeight, newNodeHeight,
        &right->height, oldRightHeight, newRightHeight,
        &parent->vNumMark, pVer, pVer + 2,
        &node->vNumMark, nVer, nVer + 2,
        &right->vNumMark, rVer, rVer + 2);

    if (kcas::execute())
        return RetCode::SUCCESS;
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::rotateLeftRight(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *leftRight, casword_t lrVer, Node<K, V> *leftLeft, casword_t llVer) {
    /***Pointers to Parents and Children***/
    //could fail fast here, should consider
    if (parent->right == node) {
        kcas::add(&parent->right, node, leftRight);
    } else if (parent->left == node) {
        kcas::add(&parent->left, node, leftRight);
    } else {
        return RetCode::FAILURE;
    }

    Node<K, V> *leftRightLeft = leftRight->left;
    if (leftRightLeft != NULL) {
        casword_t lrlVer = kcas::visit(leftRightLeft);
        kcas::add(
            &leftRightLeft->parent, leftRight, left,
            &leftRightLeft->vNumMark, lrlVer, lrlVer + 2);
    }

    Node<K, V> *leftRightRight = leftRight->right;
    if (leftRightRight != NULL) {
        casword_t lrrVer = kcas::visit(leftRightRight);
        kcas::add(
            &leftRightRight->parent, leftRight, node,
            &leftRightRight->vNumMark, lrrVer, lrrVer + 2);
    }

    if (right != NULL) {
        kcas::add(&right->vNumMark, rVer, rVer);
    }

    if (leftLeft != NULL) {
        kcas::add(&leftLeft->vNumMark, llVer, llVer);
    }

    int oldNodeHeight = node->height;
    int oldLeftHeight = left->height;
    int oldLeftRightHeight = leftRight->height;

    int newNodeHeight = 1 + max(getHeight(leftRightRight), getHeight(right));
    int newLeftHeight = 1 + max(getHeight(leftLeft), getHeight(leftRightLeft));
    int newLeftRightHeight = 1 + max(newNodeHeight, newLeftHeight);

    kcas::add(
        &leftRight->parent, left, parent,
        &leftRight->left, leftRightLeft, left,
        &left->parent, node, leftRight,
        &leftRight->right, leftRightRight, node,
        &node->parent, parent, leftRight,
        &left->right, leftRight, leftRightLeft,
        &node->left, left, leftRightRight,
        &node->height, oldNodeHeight, newNodeHeight,
        &left->height, oldLeftHeight, newLeftHeight,
        &leftRight->height, oldLeftRightHeight, newLeftRightHeight,
        &leftRight->vNumMark, lrVer, lrVer + 2,
        &parent->vNumMark, pVer, pVer + 2,
        &node->vNumMark, nVer, nVer + 2,
        &left->vNumMark, lVer, lVer + 2);

    if (kcas::execute())
        return RetCode::SUCCESS;
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::rotateRightLeft(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, Node<K, V> *left, casword_t lVer, Node<K, V> *right, casword_t rVer, Node<K, V> *rightLeft, casword_t rlVer, Node<K, V> *rightRight, casword_t rrVer) {

    if (parent->right == node) {
        kcas::add(&parent->right, node, rightLeft);
    } else if (parent->left == node) {
        kcas::add(&parent->left, node, rightLeft);
    } else {
        return RetCode::FAILURE;
    }

    Node<K, V> *rightLeftRight = rightLeft->right;
    if (rightLeftRight != NULL) {
        casword_t rlrVer = kcas::visit(rightLeftRight);

        kcas::add(
            &rightLeftRight->parent, rightLeft, right,
            &rightLeftRight->vNumMark, rlrVer, rlrVer + 2);
    }

    Node<K, V> *rightLeftLeft = rightLeft->left;
    if (rightLeftLeft != NULL) {
        casword_t rllVer = kcas::visit(rightLeftLeft);

        kcas::add(
            &rightLeftLeft->parent, rightLeft, node,
            &rightLeftLeft->vNumMark, rllVer, rllVer + 2);
    }

    if (left != NULL) {
        kcas::add(&left->vNumMark, lVer, lVer);
    }

    if (rightRight != NULL) {
        kcas::add(&rightRight->vNumMark, rrVer, rrVer);
    }

    int oldNodeHeight = node->height;
    int oldRightHeight = right->height;
    int oldRightLeftHeight = rightLeft->height;

    int newNodeHeight = 1 + max(getHeight(rightLeftLeft), getHeight(left));
    int newRightHeight = 1 + max(getHeight(rightRight), getHeight(rightLeftRight));
    int newRightLeftHeight = 1 + max(newNodeHeight, newRightHeight);

    kcas::add(
        &rightLeft->parent, right, parent,
        &rightLeft->right, rightLeftRight, right,
        &right->parent, node, rightLeft,
        &rightLeft->left, rightLeftLeft, node,
        &node->parent, parent, rightLeft,
        &right->left, rightLeft, rightLeftRight,
        &node->right, right, rightLeftLeft,
        &node->height, oldNodeHeight, newNodeHeight,
        &right->height, oldRightHeight, newRightHeight,
        &rightLeft->height, oldRightLeftHeight, newRightLeftHeight,
        &rightLeft->vNumMark, rlVer, rlVer + 2,
        &parent->vNumMark, pVer, pVer + 2,
        &node->vNumMark, nVer, nVer + 2,
        &right->vNumMark, rVer, rVer + 2);

    if (kcas::execute())
        return RetCode::SUCCESS;
    return RetCode::FAILURE;
}

template <class RecordManager, typename K, typename V>
inline int InternalKCAS<RecordManager, K, V>::countChildren(const int tid, Node<K, V> *node) {
    return (node->left == NULL ? 0 : 1) +
           (node->right == NULL ? 0 : 1);
}

template <class RecordManager, typename K, typename V>
long InternalKCAS<RecordManager, K, V>::validateSubtree(Node<K, V> *node, long smaller, long larger, std::unordered_set<casword_t> &keys, ofstream &graph, ofstream &log, bool &errorFound) {

    if (node == NULL)
        return 0;
    graph << "\"" << node << "\""
          << "[label=\"K: " << node->key << " - H: "
          << node->height << "\"];\n";

    if (IS_MARKED(node->vNumMark)) {
        log << "MARKED NODE! " << node->key << "\n";
        errorFound = true;
    }
    Node<K, V> *nodeLeft = node->left;
    Node<K, V> *nodeRight = node->right;

    if (nodeLeft != NULL) {
        graph << "\"" << node << "\" -> \"" << nodeLeft << "\"";
        if (node->key < nodeLeft->key) {
            errorFound = true;
            graph << "[color=red]";
        } else {
            graph << "[color=blue]";
        }

        graph << ";\n";
    }

    if (nodeRight != NULL) {
        graph << "\"" << node << "\" -> \"" << nodeRight << "\"";
        if (node->key > nodeRight->key) {
            errorFound = true;
            graph << "[color=red]";
        } else {
            graph << "[color=green]";
        }
        graph << ";\n";
    }

    Node<K, V> *parent = node->parent;
    graph << "\"" << node << "\" -> \"" << parent << "\""
                                                     "[color=grey];\n";
    casword_t height = node->height;

    if (!(keys.count(node->key) == 0)) {
        log << "DUPLICATE KEY! " << node->key << "\n";
        errorFound = true;
    }

    if (!((nodeLeft == NULL || nodeLeft->parent == node) &&
          (nodeRight == NULL || nodeRight->parent == node))) {
        log << "IMPROPER PARENT! " << node->key << "\n";
        errorFound = true;
    }

    if ((node->key < smaller) || (node->key > larger)) {
        log << "IMPROPER LOCAL TREE! " << node->key << "\n";
        errorFound = true;
    }

    if (nodeLeft == NULL && nodeRight == NULL && getHeight(node) > 1) {
        log << "Leaf with height > 1! " << node->key << "\n";
        errorFound = true;
    }

    keys.insert(node->key);

    long lHeight = validateSubtree(node->left, smaller, node->key, keys, graph, log, errorFound);
    long rHeight = validateSubtree(node->right, node->key, larger, keys, graph, log, errorFound);

    long ret = 1 + max(lHeight, rHeight);

    if (node->height != ret) {
        log << "Node " << node->key << " with height " << ret << " thinks it has height " << node->height << "\n";
        errorFound = true;
    }

    if (abs(lHeight - rHeight) > 1) {
        log << "Imbalanced Node! " << node->key << "(" << lHeight << ", " << rHeight << ") - " << node->height << "\n";
        errorFound = true;
    }

    return ret;
}

template <class RecordManager, typename K, typename V>
bool InternalKCAS<RecordManager, K, V>::validate() {
    // std::unordered_set<casword_t> keys = {};
    // bool errorFound;

    // rename("graph.dot", "graph_before.dot");
    // ofstream graph;
    // graph.open("graph.dot");
    // graph << "digraph G {\n";

    // ofstream log;
    // log.open("log.txt", std::ofstream::out | std::ofstream::app);

    // auto t = std::time(nullptr);
    // auto tm = *std::localtime(&t);
    // log << "Run at: " << std::put_time(&tm, "%d-%m-%Y %H-%M-%S") << "\n";

    // long ret = validateSubtree(root->left, minKey, maxKey, keys, graph, log, errorFound);
    // graph << "}";
    // graph.close();

    // if (!errorFound) {
    //     log << "Validated Successfully!\n";
    // }

    // log.close();

    // return !errorFound;
    return true;
}

template <class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::printDebuggingDetails() {
}

template <class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::freeSubtree(const int tid, Node<K, V> *node) {
    if (node == NULL)
        return;
    freeSubtree(tid, node->left);
    freeSubtree(tid, node->right);
    recmgr->deallocate(tid, node);
}