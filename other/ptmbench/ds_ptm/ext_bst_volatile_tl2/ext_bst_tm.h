/**
 * Trevor Brown, 2020.
 */

#pragma once

#include "tmlib/tm.h"
#include "record_manager.h"

#define nodeptr Node<K,V> *
#define nodeptr_unsafe Node<K,V,UNSAFE> *

template <class K, class V, tx_safety mode = SAFE>
class Node {
public:
    tx_field<mode, V                > value;
    tx_field<mode, K                > key;
    tx_field<mode, Node<K,V,mode> * > left;
    tx_field<mode, Node<K,V,mode> * > right;
};

template <class K, class V, class Compare, class RecManager>
class ext_bst_tm {
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
private:
PAD;
public:

    /**
     * This function must be called once by each thread that will
     * invoke any functions on this class.
     *
     * It must be okay that we do this with the main thread and later with another thread!!!
     */
    void initThread(const int tid) {
        TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
        recmgr->initThread(tid);
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }

private:
    inline nodeptr createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right) {
        nodeptr newnode = recmgr->template allocate<Node<K,V,SAFE> >(tid);
        if (newnode == NULL) setbench_error("could not allocate node");
        newnode->key = key;
        newnode->value = value;
        newnode->left = left;
        newnode->right = right;
        return newnode;
    }
    void dfsDeallocateBottomUp(nodeptr_unsafe u, int *numNodes) {
        if (u == NULL) return;
        if (u->left != NULL) {
            dfsDeallocateBottomUp(u->left, numNodes);
            dfsDeallocateBottomUp(u->right, numNodes);
        }
        MEMORY_STATS ++(*numNodes);
        const int tid = 0;
        recmgr->deallocate(tid, u);
    }

public:
    ext_bst_tm(const K _NO_KEY,
                const V _NO_VALUE,
                const int numProcesses)
        :         NO_KEY(_NO_KEY)
                , NO_VALUE(_NO_VALUE)
                , recmgr(new RecManager(numProcesses))
    {
        VERBOSE DEBUG COUTATOMIC("constructor ext_bst_tm"<<std::endl);
        if (numProcesses >= MAX_THREADS_POW2) setbench_error("number of threads must be smaller than MAX_THREADS_POW2");

        cmp = Compare();
        const int tid = 0;
        initThread(tid);

        TM_BEGIN();
        nodeptr rootleft = createNode(tid, NO_KEY, NO_VALUE, NULL, NULL);
        nodeptr _root = createNode(tid, NO_KEY, NO_VALUE, rootleft, NULL);
        root = _root;
        TM_END();
    }
    ~ext_bst_tm() {
        VERBOSE DEBUG COUTATOMIC("destructor ext_bst_tm");
        int numNodes = 0;
        dfsDeallocateBottomUp(root, &numNodes);
        VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<std::endl);
        recmgr->printStatus();
        delete recmgr;
    }

private:
    V doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent);

public:
    V insert(const int tid, const K& key, const V& val) { return doInsert(tid, key, val, false); }
    V insertIfAbsent(const int tid, const K& key, const V& val) { return doInsert(tid, key, val, true); }
    V erase(const int tid, const K& key);
    V find(const int tid, const K& key);
    int rangeUpdate(const int tid, const K& low, const K& hi);
    bool contains(const int tid, const K& key) { return find(tid, key) != NO_VALUE; }

    RecManager * debugGetRecMgr() { return recmgr; }
    nodeptr_unsafe debug_getEntryPoint() { return (nodeptr_unsafe) root; }


    void debugPrintTree(nodeptr root) {
        if (root == nullptr) {
            return;
        }

        printf("Node: %p, left %p, right %p, key = %lld\n", root, root->left.load(), root->right.load(), root->key.load());
        debugPrintTree(root->left);
        debugPrintTree(root->right);
    }
};

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::find(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid, true);
    TM_BEGIN();
    nodeptr l = root->left;
    l = l->left;
    if (l == NULL) {
        TM_END();
        return NO_VALUE;
    }
    while (l->left) l = (l->key == NO_KEY || cmp(key, l->key)) ? l->left : l->right;
    V result = (key == l->key) ? l->value : NO_VALUE;
    TM_END();
    return result;
}

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
    auto guard = recmgr->getGuard(tid);
    TM_BEGIN();
    // debugPrintTree(root);
    nodeptr p = root;
    nodeptr l = p->left;
    while (l->left) {
        p = l;
        l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;
    }
    // if we find the key in the tree already
    if (key == l->key) {
        V result = l->value;
        if (!onlyIfAbsent) {
            l->value = val;
        }
        TM_END();
        return result;
    } else {
        nodeptr newLeaf = createNode(tid, key, val, NULL, NULL);
        nodeptr newParent = (l->key == NO_KEY || cmp(key, l->key))
            ? createNode(tid, l->key, l->value, newLeaf, l)
            : createNode(tid, key, val, l, newLeaf);

        (l == p->left ? p->left : p->right) = newParent;

        // printf("Inserted %lld\n", key);
        // debugPrintTree(root);
        // printf("\n");

        TM_END();    
        return NO_VALUE;
    }
}

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    auto guard = recmgr->getGuard(tid);
    TM_BEGIN();
    nodeptr gp = root;
    nodeptr p = gp->left;
    nodeptr l = p->left;
    if (l == NULL) { // tree is empty
        TM_END();
        return NO_VALUE;
    }
    while (l->left) {
        gp = p;
        p = l;
        l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;
    }
    // if we fail to find the key in the tree
    if (key != l->key) {
        TM_END();
        return NO_VALUE;
    } else {
        V result = l->value;
        nodeptr s = (l == p->left ? p->right : p->left);
        (p == gp->left ? gp->left : gp->right) = s;
        TM_END();
        recmgr->retire(tid, p);
        recmgr->retire(tid, l);
        return result;
    }
}

template<class K, class V, class Compare, class RecManager>
int ext_bst_tm<K,V,Compare,RecManager>::rangeUpdate(const int tid, const K& low, const K& hi) {
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

        // if internal node, explore its children
        K key = node->key;
        if (left != NULL) {
            if (key != this->NO_KEY && !cmp(hi, key)) {
                stack.push(node->right);
            }
            if (key == this->NO_KEY || cmp(low, key)) {
                stack.push(left);
            }

        // else if leaf node, check if we should return it
        } else {
            if (key != this->NO_KEY && !cmp(key, low) && !cmp(hi, key)) {
                V v = node->value;
                node->value = (V) ((uintptr_t) v + 1);
                ++result;
            }
        }
    }
    TM_END();
    return result;
}
