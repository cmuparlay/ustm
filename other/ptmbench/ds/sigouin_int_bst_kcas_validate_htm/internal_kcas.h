#pragma once
#include <cassert>
#include <unordered_set>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <ctime>
#include <immintrin.h>
using namespace std;

#ifndef MAX_THREADS_POW2
    #define MAX_THREADS_POW2 512
#endif
#define MAX_PATH_SIZE 128
#define PADDING_BYTES 128
#define IS_MARKED(word) (word & 0x1)
#define MAX_KCAS 21
#include "kcas.h"

template<typename K, typename V>
struct Node {
    casword<casword_t> vNumMark;
    casword<K> key;
    casword<Node<K, V> *> left;
    casword<Node<K, V> *> right;
    // casword<Node<K, V> *> parent;
    // casword<int> height;
    casword<V> value;
};

enum RetCode : int {
    RETRY = 0,
    FAILURE = -1,
    SUCCESS = 1,
};

template<class RecordManager, typename K, typename V>
class InternalKCAS {
private:
    struct ObservedNode {
        ObservedNode() {}
        Node<K, V> * node = NULL;
        casword_t oVNumMark = -1;
    };

    struct PathContainer {
        ObservedNode path[MAX_PATH_SIZE];
        volatile char padding[PADDING_BYTES];
    };

    volatile char padding0[PADDING_BYTES];
    const int numThreads;
    const int minKey;
    const long long maxKey;
    volatile char padding4[PADDING_BYTES];
    Node<K, V> * root;
    volatile char padding5[PADDING_BYTES];
    RecordManager * const recmgr;
    volatile char padding7[PADDING_BYTES];
    PathContainer paths[MAX_THREADS_POW2];
    volatile char padding8[PADDING_BYTES];

public:
    InternalKCAS(const int _numThreads, const int _minKey, const long long _maxKey);
    ~InternalKCAS();
    bool contains(const int tid, const K &key);
    V insertIfAbsent(const int tid, const K &key, const V &value);
    V erase(const int tid, const K &key);
    bool validate();
    void printDebuggingDetails();
    Node<K, V> * getRoot();
    void initThread(const int tid);
    void deinitThread(const int tid);

private:
    Node<K, V> * createNode(const int tid, Node<K, V> * parent, K key, V value);
    void freeSubtree(const int tid, Node<K, V> * node);
    long validateSubtree(Node<K, V> * node, long smaller, long larger, std::unordered_set<casword_t> &keys, ofstream &graph, ofstream &log, bool &errorFound);
        int internalErase(const int tid, Node<K, V> *parent, casword_t pVer, Node<K, V> *node, casword_t nVer, const K &key);
    int internalInsert(const int tid, Node<K, V> *parent, casword_t pVer, const K &key, const V &value);
    int countChildren(const int tid, Node<K, V> * node);
    int getSuccessor(const int tid, Node<K, V> *node, Node<K, V> *&succParent, casword_t &spVer, Node<K, V> *&succ, casword_t &sVer);
    int search(const int tid, const K &key);
    int search(const int tid, Node<K, V> *&parent, casword_t &pVer, Node<K, V> *&node, casword_t &nVer, const K &key);
};

template<class RecordManager, typename K, typename V>
Node<K, V> * InternalKCAS<RecordManager, K, V>::createNode(const int tid, Node<K, V> * parent, K key, V value) {
    Node<K, V> * node = recmgr->template allocate<Node<K, V> >(tid);
    node->key.setInitVal(key);
    node->value.setInitVal(value);
    // node->parent.setInitVal(parent);
    node->vNumMark.setInitVal(0);
    node->left.setInitVal(NULL);
    node->right.setInitVal(NULL);
    return node;
}

template<class RecordManager, typename K, typename V>
InternalKCAS<RecordManager, K, V>::InternalKCAS(const int _numThreads, const int _minKey, const long long _maxKey)
: numThreads(_numThreads), minKey(_minKey), maxKey(_maxKey), recmgr(new RecordManager(numThreads)) {
    assert(_numThreads < MAX_THREADS_POW2);
    const int tid = 0;
    initThread(tid);
    root = createNode(0, NULL, (maxKey + 1 & 0x00FFFFFFFFFFFFFF), NULL);
}

template<class RecordManager, typename K, typename V>
InternalKCAS<RecordManager, K, V>::~InternalKCAS() {
    int tid = 0;
    initThread(tid);
    freeSubtree(tid, root);
    deinitThread(tid);
    delete recmgr;
}

template<class RecordManager, typename K, typename V>
inline Node<K, V> * InternalKCAS<RecordManager, K, V>::getRoot() {
    return root->left;
}

template<class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::initThread(const int tid) {
    recmgr->initThread(tid);
}

template<class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::deinitThread(const int tid) {
    recmgr->deinitThread(tid);
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
    auto guard = recmgr->getGuard(tid);
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
            // &reroute->parent, node, parent,
            &reroute->vNumMark, rVer, rVer + 2,
            &node->vNumMark, nVer, nVer + 3,
            &parent->vNumMark, pVer, pVer + 2);

        if (kcas::execute()) {
            assert(IS_MARKED(node->vNumMark));
            recmgr->retire(tid, node);

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

            // kcas::add(
            //     &succRight->parent, succ, succParent,
            //     &succRight->vNumMark, srVer, srVer + 2);
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
            return RetCode::SUCCESS;
        }
        return RetCode::RETRY;
    }
    assert(false);
    return RetCode::RETRY;
}

template<class RecordManager, typename K, typename V>
inline int InternalKCAS<RecordManager, K, V>::countChildren(const int tid, Node<K, V> * node) {
    return(node->left == NULL ? 0 : 1) +
            (node->right == NULL ? 0 : 1);
}

template <class RecordManager, typename K, typename V>
long InternalKCAS<RecordManager, K, V>::validateSubtree(Node<K, V> *node, long smaller, long larger, std::unordered_set<casword_t> &keys, ofstream &graph, ofstream &log, bool &errorFound) {

    if (node == NULL)
        return 0;
    graph << "\"" << node << "\""
          << "[label=\"K: " << node->key << "\"];\n";

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

    // Node<K, V> *parent = node->parent;
    // graph << "\"" << node << "\" -> \"" << parent << "\""
    //                                                  "[color=grey];\n";

    if (!(keys.count(node->key) == 0)) {
        log << "DUPLICATE KEY! " << node->key << "\n";
        errorFound = true;
    }

    // if (!((nodeLeft == NULL || nodeLeft->parent == node) &&
    //       (nodeRight == NULL || nodeRight->parent == node))) {
    //     log << "IMPROPER PARENT! " << node->key << "\n";
    //     errorFound = true;
    // }

    if ((node->key < smaller) || (node->key > larger)) {
        log << "IMPROPER LOCAL TREE! " << node->key << "\n";
        errorFound = true;
    }


    keys.insert(node->key);

    validateSubtree(node->left, smaller, node->key, keys, graph, log, errorFound);
    validateSubtree(node->right, node->key, larger, keys, graph, log, errorFound);

    return 1;
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

template<class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::printDebuggingDetails() {
}

template<class RecordManager, typename K, typename V>
void InternalKCAS<RecordManager, K, V>::freeSubtree(const int tid, Node<K, V> * node) {
    if(node == NULL) return;
    freeSubtree(tid, node->left);
    freeSubtree(tid, node->right);
    recmgr->deallocate(tid, node);
}