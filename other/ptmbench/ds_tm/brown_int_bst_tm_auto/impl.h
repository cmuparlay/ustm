#pragma once

#include <cstdio>
#include "tm.h"
#include "record_manager.h"

#ifndef TRACE
    #define TRACE if(0)
#endif

template <typename K, typename V, tx_safety mode = SAFE>
class Node {
public:
    tx_field<mode, V                > value;
    tx_field<mode, K                > key;
    tx_field<mode, Node<K,V,mode> * > left;
    tx_field<mode, Node<K,V,mode> * > right;
};
#define nodeptr Node<K,V,SAFE> *
#define nodeptr_unsafe Node<K,V,UNSAFE> *

thread_local void * to_undo_allocation = NULL;

template <typename K, typename V, class Compare, class RecManager>
class brown_int_bst_tm_auto {
private:
PAD;
    RecManager * const recmgr;
PAD;
    nodeptr root;        // actually const
    Compare cmp;
PAD;

public:
    const K NO_KEY;
    const V NO_VALUE;
PAD;

public:

    brown_int_bst_tm_auto(const K _NO_KEY, const V _NO_VALUE, const int numProcesses);
    ~brown_int_bst_tm_auto();
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
    V erase(const int tid, const K& key);
    int rangeUpdate(const int tid, const K& low, const K& hi);
    nodeptr_unsafe debug_getEntryPoint() { return (nodeptr_unsafe) root; }

    RecManager * debugGetRecMgr() { return recmgr; }

private:
    inline nodeptr createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right);
    inline std::pair<nodeptr, nodeptr> doSearch(const int tid, const K& key);
    inline std::pair<nodeptr, nodeptr> doSearchSuccessor(const int tid, nodeptr node);
    inline V doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent);
    void freeSubtree(nodeptr_unsafe const node) {
        if (node == NULL) return;
        freeSubtree(node->left);
        freeSubtree(node->right);
        const int dummy_tid = 0;
        recmgr->deallocate(dummy_tid, (nodeptr) node);
    }
};

template<class K, class V, class Compare, class RecManager>
brown_int_bst_tm_auto<K,V,Compare,RecManager>::brown_int_bst_tm_auto(const K _NO_KEY, const V _NO_VALUE, const int numProcesses)
        : recmgr(new RecManager(numProcesses))
        , NO_KEY(_NO_KEY)
        , NO_VALUE(_NO_VALUE)
{
    VERBOSE DEBUG COUTATOMIC("constructor brown_int_bst_tm_auto"<<std::endl);
    if (numProcesses >= MAX_THREADS_POW2) setbench_error("number of threads must be smaller than MAX_THREADS_POW2");

    cmp = Compare();
    const int tid = 0;
    initThread(tid);

    TM_BEGIN();
    root = createNode(tid, NO_KEY, NO_VALUE, NULL, NULL);
    TM_END();
}

template<class K, class V, class Compare, class RecManager>
brown_int_bst_tm_auto<K,V,Compare,RecManager>::~brown_int_bst_tm_auto() {
    VERBOSE DEBUG COUTATOMIC("destructor brown_int_bst_tm_auto");
    int tid = 0;
    initThread(tid);
    freeSubtree((nodeptr_unsafe) root);
    recmgr->printStatus();
    deinitThread(tid);
    delete recmgr;
}

template<class K, class V, class Compare, class RecManager>
nodeptr brown_int_bst_tm_auto<K,V,Compare,RecManager>::createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right) {
    nodeptr newnode = recmgr->template allocate<Node<K,V,SAFE>>(tid);
    if (newnode == NULL) setbench_error("could not allocate node");
    newnode->key = key;
    newnode->value = value;
    newnode->left = left;
    newnode->right = right;
    return newnode;
}

template<class K, class V, class Compare, class RecManager>
std::pair<nodeptr, nodeptr> brown_int_bst_tm_auto<K,V,Compare,RecManager>::doSearch(const int tid, const K& key) {
    nodeptr p = NULL;
    nodeptr l = root;
    while (true) {
        K currKey = l->key;
        if (currKey == key) return std::pair<nodeptr, nodeptr>(l, p);
        nodeptr next = (currKey == NO_KEY || cmp(key, currKey)) ? l->left : l->right;
        if (!next) break;
        p = l;
        l = next;
    }
    return std::pair<nodeptr, nodeptr>(l, p);
}

template<class K, class V, class Compare, class RecManager>
std::pair<nodeptr, nodeptr> brown_int_bst_tm_auto<K,V,Compare,RecManager>::doSearchSuccessor(const int tid, nodeptr node) {
    nodeptr p = node;
    nodeptr l = node->right;
    while (l->left) {
        p = l;
        l = l->left;
    }
    // TRACE printf("    doSearchSuccessor(key %lld) returned nodes l:key%lld and p:key%lld\n", node->key.load(), l->key.load(), p->key.load());
    return std::pair<nodeptr, nodeptr>(l, p);
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm_auto<K,V,Compare,RecManager>::find(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN_RO();
    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    V retval = (key == l->key) ? l->value : NO_VALUE;
    TM_END();
    return retval;
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm_auto<K,V,Compare,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    auto guard = recmgr->getGuard(tid);

    to_undo_allocation = NULL;
    TM_BEGIN();
    if (to_undo_allocation) {
        recmgr->deallocate(tid, (nodeptr) to_undo_allocation);
        to_undo_allocation = NULL;
    }

    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    K lkey = l->key;
    // if we find the key in the tree already
    if (key == lkey) {
        // printf("#### thread %3d: read-only INSERT %lld\n", __tm.initter_id, lkey);
        V result = l->value;
        assert(result != NO_VALUE);
        if (!onlyIfAbsent) {
            l->value = val;
        }
        TM_END();
        return result;
    } else {
        // printf("#### thread %3d: LEAF INSERT %lld\n", __tm.initter_id, lkey);
        nodeptr lleft = l->left;
        nodeptr lright = l->right;

        nodeptr newLeaf = createNode(tid, key, val, NULL, NULL);
        to_undo_allocation = newLeaf;
        if (lkey == NO_KEY || cmp(key, lkey)) {
            assert(lleft == NULL);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
            l->left = newLeaf;
        } else {
            assert(lright == NULL);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
            l->right = newLeaf;
        }
        TM_END();
        return NO_VALUE;
    }
    assert(false);
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm_auto<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid);

    TM_BEGIN();
    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    nodeptr p = ret.second;

    nodeptr to_retire = NULL;
    nodeptr lleft = l->left;
    nodeptr lright = l->right;
    K lkey = l->key;

    // if we fail to find the key in the tree
    if (key != lkey) {
        // printf("#### thread %3d: read-only ERASE %lld\n", __tm.initter_id, lkey);
        // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
        TM_END();
        return NO_VALUE;

    // if we find the key
    } else {
        nodeptr pleft = p->left;
        nodeptr pright = p->right;
        V retval = l->value;

        // leaf delete
        if (lleft == NULL && lright == NULL) {
            assert(key == lkey);
            // printf("#### thread %3d: LEAF ERASE %lld\n", __tm.initter_id, lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
            if (l == pleft) {
                p->left = NULL;
            } else {
                p->right = NULL;
            }
            to_retire = l;

        // one child delete
        } else if (lleft == NULL || lright == NULL) {
            assert(key == lkey);
            // printf("#### thread %3d: ONE CHILD ERASE %lld\n", __tm.initter_id, lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
            nodeptr otherNode = lleft ? lleft : lright;
            if (l == pleft) {
                p->left = otherNode;
            } else {
                p->right = otherNode;
            }
            to_retire = l;

        // two child delete
        } else {
            assert(key == lkey);
            // printf("#### thread %3d: TWO CHILD ERASE %lld\n", __tm.initter_id, lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? lleft->key : (K) -1), (lright ? lright->key : (K) -1));
            std::pair<nodeptr, nodeptr> succPair = doSearchSuccessor(tid, l);
            nodeptr succ = succPair.first;
            nodeptr succParent = succPair.second;

            l->value = succ->value.load();
            l->key = succ->key.load();

            nodeptr succLeft = succ->left;
            nodeptr succRight = succ->right;
            assert(!succRight || succRight->key != NO_KEY);

            assert(!succLeft);
            if (succ == succParent->left) {
                succParent->left = succRight;
            } else {
                succParent->right = succRight;
            }
            to_retire = succ;
        }

        TM_END();
        if (to_retire) recmgr->retire(tid, to_retire);
        return retval;
    }
    assert(false);
}

template<class K, class V, class Compare, class RecManager>
int brown_int_bst_tm_auto<K,V,Compare,RecManager>::rangeUpdate(const int tid, const K& low, const K& hi) {
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
