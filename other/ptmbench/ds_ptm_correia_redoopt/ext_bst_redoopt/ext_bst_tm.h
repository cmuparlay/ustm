/**
 * Trevor Brown, 2020.
 */

#pragma once

#define USE_ESLOCO
#define PM_FILE_NAME   "/mnt/pmem1_mount/redoopt_guy"

#include "otherPtms/correia-cx-and-redo/redoopt/RedoOpt.hpp"
#include "record_manager.h"

#define TM_FIELD redoopt::persist
#define TM redoopt::RedoOpt

#define nodeptr Node<K,V> *
#define nodeptr_unsafe Node<K,V> *

#define TM_nodeptr TM_FIELD<Node<K,V> * >

template <class K, class V>
class Node {
public:
    TM_FIELD<V> value;
    TM_FIELD<K> key;
    TM_FIELD<Node<K,V> * > left;
    TM_FIELD<Node<K,V> * > right;
};

template <class K, class V, class Compare, class RecManager>
class ext_bst_tm {
private:
PAD;
    RecManager * const recmgr;
PAD;
    // nodeptr root;        // actually const
    TM_FIELD<Node<K,V> * > root;        // actually const
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
        // recmgr->initThread(tid);
    }
    void deinitThread(const int tid) {
        // recmgr->deinitThread(tid);
    }

private:
    inline nodeptr createNode(const int tid, const K& key, const V& value, nodeptr const left, nodeptr const right) {
        // nodeptr newnode = recmgr->template allocate<Node<K,V> >(tid);
        nodeptr newnode = TM::tmNew<Node<K,V>>();
        if (newnode == NULL) setbench_error("could not allocate node");
        newnode->key = key;
        newnode->value = value;
        newnode->left = left;
        newnode->right = right;
        return newnode;
    }
    // void dfsDeallocateBottomUp(nodeptr_unsafe u, int *numNodes) {
    //     if (u == NULL) return;
    //     if (u->left != NULL) {
    //         dfsDeallocateBottomUp(u->left, numNodes);
    //         dfsDeallocateBottomUp(u->right, numNodes);
    //     }
    //     MEMORY_STATS ++(*numNodes);
    //     const int tid = 0;
    //     // recmgr->deallocate(tid, u);
    //     TM::tmDelete(u);
    // }

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

        TM::template updateTx<int>([=] () {
            nodeptr rootleft = createNode(tid, NO_KEY, NO_VALUE, NULL, NULL);
            nodeptr _root = createNode(tid, NO_KEY, NO_VALUE, rootleft, NULL);
            root = _root;
            return 0;
        });

        K key = root->key;
        nodeptr l = root->left;
        nodeptr r = root->right;
        // printf("Node: %p, left %p, right %p, key = %lld\n", root, root->left.pload(), root->right.pload(), root->key.pload());
        // printf("Node: %p, left %p, right %p, key = %lld\n", root.pload(), l, r, key);
    }
    ~ext_bst_tm() {
        // VERBOSE DEBUG COUTATOMIC("destructor ext_bst_tm");
        // int numNodes = 0;
        // dfsDeallocateBottomUp(root, &numNodes);
        // VERBOSE DEBUG COUTATOMIC(" deallocated nodes "<<numNodes<<std::endl);
        // recmgr->printStatus();
        // delete recmgr;
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

    void debugPrintTree(nodeptr node) {
        if (node == nullptr) {
            return;
        }
        K key = node->key;
        nodeptr l = node->left;
        nodeptr r = node->right;
        // printf("Node: %p, left %p, right %p, key = %lld\n", root, root->left.pload(), root->right.pload(), root->key.pload());
        // printf("Node: %p, left %p, right %p, key = %lld\n", node, l, r, key);
        // debugPrintTree(l);
        // debugPrintTree(r);
    }
};

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::find(const int tid, const K& key) {    
    return TM::template readTx<V>([=] () {
        // auto guard = recmgr->getGuard(tid, true);
        nodeptr l = root->left;
        l = l->left;
        if (l == NULL) {            
            return NO_VALUE;
        }
        while (l->left) l = (l->key == NO_KEY || cmp(key, l->key)) ? l->left : l->right;
        V result = NO_VALUE;
        if (key == l->key) {
            result = l->value;
        }                
        return result;
    });
}

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {    
    
    return TM::template updateTx<V>([&] () {
        V retVal;
        // printf("Trying insert: %lld\n", key);
        // debugPrintTree(root);
        // auto guard = recmgr->getGuard(tid);
        nodeptr p = root;
        nodeptr l = p->left;

        while (l->left) {
            p = l;
            l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;

            // if (l == nullptr) {
            //     printf("ERROR\n");
            // }
        }
        // if we find the key in the tree already
        if (key == l->key) {
            V result = l->value;
            if (!onlyIfAbsent) {
                l->value = val;
            }
            retVal = result;
            return result;            
        } else {
            TM_nodeptr newLeaf = createNode(tid, key, val, NULL, NULL);
            TM_nodeptr newParent = (l->key == NO_KEY || cmp(key, l->key))
                ? createNode(tid, l->key, l->value, newLeaf, l)
                : createNode(tid, key, val, l, newLeaf);

            // (l == p->left ? p->left : p->right) = newParent;
            if (l == p->left) {
                p->left = newParent;
            }
            else {
                p->right = newParent;
            }

            // root->left->key = 789;

            // printf("Inserted: %lld\n", key);
            // debugPrintTree(root);
            // printf("\n");
            retVal = NO_VALUE;
            return NO_VALUE;            
        }
    });

    // return retVal;
}

template<class K, class V, class Compare, class RecManager>
V ext_bst_tm<K,V,Compare,RecManager>::erase(const int tid, const K& key) {
    return TM::template updateTx<V>([&] () {
        // auto guard = recmgr->getGuard(tid);
        nodeptr gp = root;
        nodeptr p = gp->left;
        nodeptr l = p->left;
        if (l == NULL) { // tree is empty
            return NO_VALUE;
        }
        while (l->left) {
            gp = p;
            p = l;
            l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;
        }
        // if we fail to find the key in the tree
        if (key != l->key) {
            return NO_VALUE;
        } else {
            V result = l->value;
            nodeptr s = (l == p->left ? p->right : p->left);
            (p == gp->left ? gp->left : gp->right) = s;
            // recmgr->retire(tid, p);            
            // recmgr->retire(tid, l);
            TM::tmDelete(p);
            TM::tmDelete(l);
            return result;
        }
    });
}
