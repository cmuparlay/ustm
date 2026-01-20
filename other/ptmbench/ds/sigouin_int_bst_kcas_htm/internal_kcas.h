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
#define MAX_KCAS 8
#include "kcas.h"

template<typename K, typename V>
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
    int internalErase(const int tid, ObservedNode &parentObserved, ObservedNode &nodeObserved, const K &key);
    int internalInsert(const int tid, ObservedNode &parentObserved, ObservedNode &ancestorObserved, const K &key, const V &value);
    int countChildren(const int tid, Node<K, V> * node);
    int getSuccessor(const int tid, Node<K, V> * node, ObservedNode &succObserved, const K &key);
    bool validatePath(const int tid, const int &size, const K &key, ObservedNode path[]);
    int search(const int tid, const K &key);
    int search(const int tid, ObservedNode &parentObserved, ObservedNode &nodeObserved, const K &key);
    int search(const int tid, ObservedNode &parentObserved, ObservedNode &nodeObserved, ObservedNode &ancestorObserved, const K &key);
};

template<class RecordManager, typename K, typename V>
Node<K, V> * InternalKCAS<RecordManager, K, V>::createNode(const int tid, Node<K, V> * parent, K key, V value) {
    Node<K, V> * node = recmgr->template allocate<Node<K, V> >(tid);
    node->key.setInitVal(key);
    node->value.setInitVal(value);
    node->parent.setInitVal(parent);
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

/* getSuccessor(const int tid, Node * node, ObservedNode &succObserved, int key)
 * ### Gets the successor of a given node in it's subtree ###
 * returns the successor of a given node stored within an ObservedNode with the
 * observed version number.
 * Returns an integer, 1 indicating the process was successful, 0 indicating a retry
 */
template<class RecordManager, typename K, typename V>
inline int InternalKCAS<RecordManager, K, V>::getSuccessor(const int tid, Node<K, V> * node, ObservedNode &oSucc, const K &key) {
    auto & path = paths[tid].path;
    while(true) {
        Node<K, V> * succ = node->right;
        path[0].node = node;
        path[0].oVNumMark = node->vNumMark;
        int currSize = 1;

        while(succ != NULL) {
            assert(currSize < MAX_PATH_SIZE - 1);
            path[currSize].node = succ;
            path[currSize].oVNumMark = succ->vNumMark;
            currSize++;
            succ = succ->left;
        }

        if(validatePath(tid, currSize, key, path) && currSize > 1) {
            oSucc = path[currSize - 1];
            return RetCode::SUCCESS;
        }
        else {
            return RetCode::RETRY;
        }
    }
}

template<class RecordManager, typename K, typename V>
inline bool InternalKCAS<RecordManager, K, V>::contains(const int tid, const K &key) {
    assert(key <= maxKey);
    int result;
    auto guard = recmgr->getGuard(tid, true);

    while((result = search(tid, key)) == RetCode::RETRY) {
        /* keep trying until we get a result */
    }
    return result == RetCode::SUCCESS;
}

template<class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::search(const int tid, const K &key) {
    assert(key <= maxKey);

    K currKey;
    casword_t nodeVNumMark;

    auto & path = paths[tid].path;
    path[0].node = root;
    path[0].oVNumMark = root->vNumMark;

    Node<K, V> * node = root->left;

    int currSize = 1;

    while(true) {
        assert(currSize < MAX_PATH_SIZE - 1);
        // We have hit a terminal node without finding our key, must validate
        if(node == NULL) {
            if(validatePath(tid, currSize, key, path)) {
                return RetCode::FAILURE;
            }
            else {
                return RetCode::RETRY;
            }
        }
        nodeVNumMark = node->vNumMark;
        currKey = node->key;

        path[currSize].node = node;
        path[currSize].oVNumMark = nodeVNumMark;
        currSize++;

        if(key > currKey) {
            node = node->right;
        }
        else if(key < currKey) {
            node = node->left;
        } //no validation required on finding a key
        else {
            return RetCode::SUCCESS;
        }
    }
}

/* search(const int tid, ObservedNode &predObserved, ObservedNode &parentObserved, ObservedNode &nodeObserved, const int &key)
 * A proposed successor-predecessor pair is generated by searching for a given key,
 * if the key is not found, the path is then validated to ensure it was not missed.
 * Where appropriate, the predecessor (predObserved), parent (parentObserved) and node
 * (nodeObserved) are provided to the caller.
 */
template<class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::search(const int tid, ObservedNode &oParent, ObservedNode &oNode, const K &key) {
    assert(key <= maxKey);

    K currKey;
    casword_t nodeVNumMark;

    auto & path = paths[tid].path;
    path[0].node = root;
    path[0].oVNumMark = root->vNumMark;

    Node<K, V> * node = root->left;

    int currSize = 1;

    while(true) {
        assert(currSize < MAX_PATH_SIZE - 1);
        //We have hit a terminal node without finding our key, must validate
        if(node == NULL) {
            if(validatePath(tid, currSize, key, path)) {
                oParent = path[currSize - 1];
                return RetCode::FAILURE;
            }
            else {
                return RetCode::RETRY;
            }
        }

        nodeVNumMark = node->vNumMark;

        currKey = node->key;

        path[currSize].node = node;
        path[currSize].oVNumMark = nodeVNumMark;
        currSize++;

        if(key > currKey) {
            node = node->right;
        }
        else if(key < currKey) {
            node = node->left;
        } //no validation required on finding a key
        else {
            oParent = path[currSize - 2];
            oNode = path[currSize - 1];
            return RetCode::SUCCESS;
        }
    }
}

template<class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::search(const int tid, ObservedNode &oParent, ObservedNode &oNode, ObservedNode &oAnc, const K &key) {
    assert(key <= maxKey);

    K currKey;
    casword_t nodeVNumMark;

    auto & path = paths[tid].path;
    path[0].node = root;
    path[0].oVNumMark = root->vNumMark;

    Node<K, V> * node = root->left;
    int succIndex = 0;
    int predIndex = -1;

    int currSize = 1;

    while(true) {
        assert(currSize < MAX_PATH_SIZE - 1);
        //We have hit a terminal node without finding our key, must validate
        if(node == NULL) {
            if(validatePath(tid, currSize, key, path)) {
                int ancIndex = min(succIndex, predIndex);
                if(ancIndex > -1) oAnc = path[ancIndex];
                oParent = path[currSize - 1];
                return RetCode::FAILURE;
            }
            else {
                return RetCode::RETRY;
            }
        }

        nodeVNumMark = node->vNumMark;

        currKey = node->key;

        path[currSize].node = node;
        path[currSize].oVNumMark = nodeVNumMark;
        currSize++;

        if(key > currKey) {
            predIndex = currSize - 1;
            node = node->right;
        }
        else if(key < currKey) {
            succIndex = currSize - 1;
            node = node->left;
        } //no validation required on finding a key
        else {
            oParent = path[currSize - 2];
            oNode = path[currSize - 1];
            return RetCode::SUCCESS;
        }
    }
}

/* validatePath(const int tid, const int size, const int key, ObservedNode path[MAX_PATH_SIZE])
 * ### Validates all nodes in a path such that they are not marked and their version numbers have not changed ###
 * validated a given path, ensuring that all version numbers of observed nodes still match
 * the version numbers stored locally within nodes within the tree. This provides the caller
 * with certainty that there was a time that this path existed in the tree
 * Returns true for a valid path
 * Returns false for an invalid path (some node version number changed)
 */
template<class RecordManager, typename K, typename V>
inline bool InternalKCAS<RecordManager, K, V>::validatePath(const int tid, const int &size, const K &key, ObservedNode path[]) {
    assert(size > 0 && size < MAX_PATH_SIZE);

    for(int i = 0; i < size; i++) {
        ObservedNode oNode = path[i];
        if(oNode.node->vNumMark != oNode.oVNumMark || IS_MARKED(oNode.oVNumMark)) {
            return false;
        }
    }
    return true;
}

template<class RecordManager, typename K, typename V>
inline V InternalKCAS<RecordManager, K, V>::insertIfAbsent(const int tid, const K &key, const V &value) {
    ObservedNode oParent;
    ObservedNode oNode;

    while(true) {
        ObservedNode oAnc;

        auto guard = recmgr->getGuard(tid);
#if defined KCAS_HTM_FULL
        if (!kcas::start()) continue;
#endif
        int res;
        while((res = (search(tid, oParent, oNode, oAnc, key))) == RetCode::RETRY) {
            /* keep trying until we get a result */
        }

        if(res == RetCode::SUCCESS) {
            return(V) oNode.node->value;
        }

        assert(res == RetCode::FAILURE);
        if(internalInsert(tid, oParent, oAnc, key, value)) {
            return 0;
        }
    }
}

template<class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::internalInsert(const int tid, ObservedNode &oParent, ObservedNode &oAnc, const K &key, const V &value) {
#if ! defined KCAS_HTM_FULL
    kcas::start();
#endif
    Node<K, V> * parent = oParent.node;
    Node<K, V> * anc = oAnc.node;

    assert(anc != parent && (anc == NULL || ((anc->key < key && key < parent->key) || (anc->key > key && key > parent->key)) || anc->vNumMark != oAnc.oVNumMark || parent->vNumMark != oParent.oVNumMark));

    if(anc != NULL) { //Note anc can never be parent...
        kcas::add(&anc->vNumMark, oAnc.oVNumMark, oAnc.oVNumMark);
    }

    Node<K, V> * newNode = createNode(tid, parent, key, value);

    if(key > parent->key) {
        kcas::add(&parent->right, (Node<K, V> *)NULL, newNode);
    }
    else if(key < parent->key) {
        kcas::add(&parent->left, (Node<K, V> *)NULL, newNode);
    }
    else {
        recmgr->deallocate(tid, newNode);
        return RetCode::RETRY;
    }

    kcas::add(&parent->vNumMark, oParent.oVNumMark, oParent.oVNumMark + 2);

    if(kcas::execute()) {
        return RetCode::SUCCESS;
    }

    recmgr->deallocate(tid, newNode);

    return RetCode::RETRY;
}

template<class RecordManager, typename K, typename V>
inline V InternalKCAS<RecordManager, K, V>::erase(const int tid, const K &key) {
    ObservedNode oParent;
    ObservedNode oNode;

    while(true) {
        auto guard = recmgr->getGuard(tid);
#if defined KCAS_HTM_FULL
        if (!kcas::start()) continue;
#endif
        int res = 0;
        while((res = (search(tid, oParent, oNode, key))) == RetCode::RETRY) {
            /* keep trying until we get a result */
        }

        if(res == RetCode::FAILURE) {
            return 0;
        }

        assert(res == RetCode::SUCCESS);
        if((res = internalErase(tid, oParent, oNode, key))) {
            return(V) oNode.node->value;
        }
    }
}

template<class RecordManager, typename K, typename V>
int InternalKCAS<RecordManager, K, V>::internalErase(const int tid, ObservedNode &oParent, ObservedNode &oNode, const K &key) {
    Node<K, V> * parent = oParent.node;
    Node<K, V> * node = oNode.node;

    int numChildren = countChildren(tid, node);

#if ! defined KCAS_HTM_FULL
    kcas::start();
#endif

    if(IS_MARKED(oParent.oVNumMark) || IS_MARKED(oNode.oVNumMark)) {
        return RetCode::RETRY;
    }

    if(numChildren == 0) {
        /* No-Child Delete
         * Unlink node
         */

        if(key > parent->key) {
            kcas::add(&parent->right, node, (Node<K, V> *)NULL);
        }
        else if(key < parent->key) {
            kcas::add(&parent->left, node, (Node<K, V> *)NULL);
        }
        else {
            return RetCode::RETRY;
        }

        kcas::add(
                &parent->vNumMark, oParent.oVNumMark, oParent.oVNumMark + 2,
                &node->vNumMark, oNode.oVNumMark, oNode.oVNumMark + 3
                );

        if(kcas::execute()) {
            assert(IS_MARKED(node->vNumMark));
            recmgr->retire(tid, node);
            return RetCode::SUCCESS;
        }

        return RetCode::RETRY;
    }
    else if(numChildren == 1) {
        /* One-Child Delete
         * Reroute parent pointer around removed node
         */

        Node<K, V> * left = node->left;
        Node<K, V> * right = node->right;
        Node<K, V> * reroute;

        //determine which child will be the replacement
        if(left != NULL) {
            reroute = left;
        }
        else if(right != NULL) {
            reroute = right;
        }
        else {
            return RetCode::RETRY;
        }

        casword_t rerouteVNum = reroute->vNumMark;

        if(IS_MARKED(rerouteVNum)) {
            return RetCode::RETRY;
        }

        if(key > parent->key) {
            kcas::add(&parent->right, node, reroute);
        }
        else if(key < parent->key) {
            kcas::add(&parent->left, node, reroute);
        }
        else {
            return RetCode::RETRY;
        }

        kcas::add(
                &reroute->parent, node, parent,
                &reroute->vNumMark, rerouteVNum, rerouteVNum + 2,
                &node->vNumMark, oNode.oVNumMark, oNode.oVNumMark + 3,
                &parent->vNumMark, oParent.oVNumMark, oParent.oVNumMark + 2
                );

        if(kcas::execute()) {
            assert(IS_MARKED(node->vNumMark));
            recmgr->retire(tid, node);
            return RetCode::SUCCESS;
        }

        return RetCode::RETRY;
    }
    else if(numChildren == 2) {
        /* Two-Child Delete
         * Promotion of descendant successor to this node by replacing the key/value pair at the node
         */

        ObservedNode oSucc;

        //the (decendant) successor's key will be promoted
        if(getSuccessor(tid, node, oSucc, key) == RetCode::RETRY) {
            return RetCode::RETRY;
        }

        if(oSucc.node == NULL) {
            return RetCode::RETRY;
        }

        Node<K, V> * succ = oSucc.node;
        Node<K, V> * succParent = succ->parent;

        ObservedNode oSuccParent;
        oSuccParent.node = succParent;
        oSuccParent.oVNumMark = succParent->vNumMark;

        if(oSuccParent.node == NULL) {
            return RetCode::RETRY;
        }

        K succKey = succ->key;

        assert(succKey <= maxKey);

        if(IS_MARKED(oSuccParent.oVNumMark)) {
            return RetCode::RETRY;
        }

        Node<K, V> * succRight = succ->right;

        if(succRight != NULL) {
            casword_t succRightVNum = succRight->vNumMark;

            if(IS_MARKED(succRightVNum)) {
                return RetCode::RETRY;
            }

            kcas::add(
                    &succRight->parent, succ, succParent,
                    &succRight->vNumMark, succRightVNum, succRightVNum + 2
                    );
        }

        if(succParent->right == succ) {
            kcas::add(&succParent->right, succ, succRight);
        }
        else if(succParent->left == succ) {
            kcas::add(&succParent->left, succ, succRight);
        }
        else {
            return RetCode::RETRY;
        }

        V nodeVal = node->value;
        V succVal = succ->value;

        kcas::add(
                &node->value, nodeVal, succVal,
                &node->key, key, succKey,
                &succ->vNumMark, oSucc.oVNumMark, oSucc.oVNumMark + 3,
                &succParent->vNumMark, oSuccParent.oVNumMark, oSuccParent.oVNumMark + 2
                );

        if(succParent != node) {
            kcas::add(&node->vNumMark, oNode.oVNumMark, oNode.oVNumMark + 2);
        }

        if(kcas::execute()) {
            assert(IS_MARKED(succ->vNumMark));
            recmgr->retire(tid, succ);
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

template<class RecordManager, typename K, typename V>
bool InternalKCAS<RecordManager, K, V>::validate() {
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