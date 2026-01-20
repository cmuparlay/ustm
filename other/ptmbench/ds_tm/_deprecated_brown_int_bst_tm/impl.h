#pragma once

#include <cstdio>
#include "tm.h"
#include "record_manager.h"

#ifndef TRACE
    #define TRACE if(0)
#endif

template <typename K, typename V>
class Node {
public:
    V volatile value;
    K volatile key;
    Node<K,V> * volatile left;
    Node<K,V> * volatile right;
};
#define nodeptr Node<K,V> *

template <typename K, typename V, class Compare, class RecManager>
class brown_int_bst_tm {
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

    brown_int_bst_tm(const K _NO_KEY, const V _NO_VALUE, const int numProcesses);
    ~brown_int_bst_tm();
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
    nodeptr debug_getEntryPoint() { return root; }

    RecManager * debugGetRecMgr() { return recmgr; }

private:
    inline nodeptr createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right);
    inline std::pair<nodeptr, nodeptr> doSearch(const int tid, const K& key);
    inline std::pair<nodeptr, nodeptr> doSearchSuccessor(const int tid, nodeptr node);
    inline V doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent);
    void freeSubtree(nodeptr const node) {
        if (node == NULL) return;
        freeSubtree(node->left);
        freeSubtree(node->right);
        const int dummy_tid = 0;
        recmgr->deallocate(dummy_tid, node);
    }
};

template<class K, class V, class Compare, class RecManager>
brown_int_bst_tm<K,V,Compare,RecManager>::brown_int_bst_tm(const K _NO_KEY, const V _NO_VALUE, const int numProcesses)
        : recmgr(new RecManager(numProcesses))
        , NO_KEY(_NO_KEY)
        , NO_VALUE(_NO_VALUE)
{
    VERBOSE DEBUG COUTATOMIC("constructor brown_int_bst_tm"<<std::endl);
    cmp = Compare();
    const int tid = 0;
    initThread(tid);

    assert(numProcesses < MAX_THREADS_POW2);
    root = createNode(tid, NO_KEY, NO_VALUE, NULL, NULL);
}

template<class K, class V, class Compare, class RecManager>
brown_int_bst_tm<K,V,Compare,RecManager>::~brown_int_bst_tm() {
    VERBOSE DEBUG COUTATOMIC("destructor brown_int_bst_tm");
    freeSubtree(root);
    recmgr->printStatus();
    delete recmgr;
}

template<class K, class V, class Compare, class RecManager>
nodeptr brown_int_bst_tm<K,V,Compare,RecManager>::createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right) {
    nodeptr newnode = recmgr->template allocate<Node<K,V>>(tid);
    if (newnode == NULL) setbench_error("could not allocate node");
    newnode->key = key;
    newnode->value = value;
    newnode->left = left;
    newnode->right = right;
    return newnode;
}

#define LEFT(node) ({ TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; ((nodeptr) TM_SHARED_READ_P((node)->left)); })
#define RIGHT(node) ({ TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; ((nodeptr) TM_SHARED_READ_P((node)->right)); })
#define KEY(node) ({ TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; ((K) TM_SHARED_READ_P((node)->key)); })
#define VALUE(node) ({ TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; ((V) TM_SHARED_READ_P((node)->value)); })
#define WRITE_VALUE(node, x) { TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; TM_SHARED_WRITE_P((node)->value, (void *) (x)); }
#define WRITE_KEY(node, x) { TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; TM_SHARED_WRITE_P((node)->key, (void *) (x)); }
#define WRITE_LEFT(node, x) { TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; TM_SHARED_WRITE_P((node)->left, (void *) (x)); }
#define WRITE_RIGHT(node, x) { TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE; TM_SHARED_WRITE_P((node)->right, (void *) (x)); }

template<class K, class V, class Compare, class RecManager>
std::pair<nodeptr, nodeptr> brown_int_bst_tm<K,V,Compare,RecManager>::doSearch(const int tid, const K& key) {
    nodeptr p = NULL;
    nodeptr l = root;
    while (true) {
        K currKey = KEY(l);
        if (currKey == key) return std::pair<nodeptr, nodeptr>(l, p);
        nodeptr next = (currKey == NO_KEY || cmp(key, currKey)) ? LEFT(l) : RIGHT(l);
        if (!next) break;
        p = l;
        l = next;
    }
    return std::pair<nodeptr, nodeptr>(l, p);
}

template<class K, class V, class Compare, class RecManager>
std::pair<nodeptr, nodeptr> brown_int_bst_tm<K,V,Compare,RecManager>::doSearchSuccessor(const int tid, nodeptr node) {
    nodeptr p = node;
    nodeptr l = RIGHT(node);
    while (LEFT(l)) {
        p = l;
        l = LEFT(l);
    }
    // TRACE printf("    doSearchSuccessor(key %lld) returned nodes l:key%lld and p:key%lld\n", KEY(node), KEY(l), KEY(p));
    return std::pair<nodeptr, nodeptr>(l, p);
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm<K,V,Compare,RecManager>::find(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN_RO();
    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    V retval = (key == KEY(l)) ? VALUE(l) : NO_VALUE;
    TM_END();
    return retval;
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm<K,V,Compare,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    auto guard = recmgr->getGuard(tid);

    TM_BEGIN();

    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    K lkey = KEY(l);
    // if we find the key in the tree already
    if (key == lkey) {
        V result = VALUE(l);
        assert(result != NO_VALUE);
        if (!onlyIfAbsent) {
            WRITE_VALUE(l, val);
        }
        TM_END();
        return result;
    } else {
        nodeptr lleft = LEFT(l);
        nodeptr lright = RIGHT(l);

        nodeptr newLeaf = createNode(tid, key, val, NULL, NULL);
        if (lkey == NO_KEY || cmp(key, lkey)) {
            assert(lleft == NULL);
            // TRACE printf("doInsert inserted %lld left of %lld\n", KEY(newLeaf), lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
            WRITE_LEFT(l, newLeaf);
        } else {
            assert(lright == NULL);
            // TRACE printf("doInsert inserted %lld right of %lld\n", KEY(newLeaf), lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
            WRITE_RIGHT(l, newLeaf);
        }
        TM_END();
        return NO_VALUE;
    }
    assert(false);
}

template<class K, class V, class Compare, class RecManager>
V brown_int_bst_tm<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid);

    TM_BEGIN();
    std::pair<nodeptr, nodeptr> ret = doSearch(tid, key);
    nodeptr l = ret.first;
    nodeptr p = ret.second;

    nodeptr to_retire = NULL;
    nodeptr lleft = LEFT(l);
    nodeptr lright = RIGHT(l);
    K lkey = KEY(l);

    // if we fail to find the key in the tree
    if (key != lkey) {
        // TRACE printf("erase finds %lld instead of %lld\n", lkey, key);
        // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
        TM_END();
        return NO_VALUE;

    // if we find the key
    } else {
        nodeptr pleft = LEFT(p);
        nodeptr pright = RIGHT(p);
        V retval = VALUE(l);

        // leaf delete
        if (lleft == NULL && lright == NULL) {
            assert(key == lkey);
            // TRACE printf("erase LEAF erase %lld\n", lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
            if (l == pleft) {
                WRITE_LEFT(p, NULL);
            } else {
                WRITE_RIGHT(p, NULL);
            }
            to_retire = l;

        // one child delete
        } else if (lleft == NULL || lright == NULL) {
            assert(key == lkey);
            // TRACE printf("erase ONE CHILD erase %lld\n", lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
            nodeptr otherNode = lleft ? lleft : lright;
            if (l == pleft) {
                WRITE_LEFT(p, otherNode);
            } else {
                WRITE_RIGHT(p, otherNode);
            }
            to_retire = l;

        // two child delete
        } else {
            assert(key == lkey);
            // TRACE printf("erase TWO CHILD erase %lld\n", lkey);
            // TRACE printf("    left=%lld right=%lld\n", (lleft ? KEY(lleft) : (K) -1), (lright ? KEY(lright) : (K) -1));
            std::pair<nodeptr, nodeptr> succPair = doSearchSuccessor(tid, l);
            nodeptr succ = succPair.first;
            nodeptr succParent = succPair.second;

            V succval = VALUE(succ);
            K succkey = KEY(succ);

            WRITE_VALUE(l, succval);
            WRITE_KEY(l, succkey);

            nodeptr succLeft = LEFT(succ);
            nodeptr succRight = RIGHT(succ);
            nodeptr succParentLeft = LEFT(succParent);

            assert(!succLeft);
            if (succ == succParentLeft) {
                WRITE_LEFT(succParent, succRight);
            } else {
                WRITE_RIGHT(succParent, succRight);
            }
            to_retire = succ;
        }

        TM_END();
        if (to_retire) recmgr->retire(tid, to_retire);
        return retval;
    }
    assert(false);
}
