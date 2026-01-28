/**
 * Trevor Brown, 2020.
 */

#pragma once

#include "tm.h"

template <class K, class V>
class Node {
public:
    TM_TYPE_T<V> value;
    TM_TYPE_T<K> key;
    TM_TYPE_T<Node<K,V>*> left;
    TM_TYPE_T<Node<K,V>*> right;
};

template <class K, class V, class Compare>
class ext_bst_tm {
public:
    PAD;
    Node<K,V>* root;        // actually const
    Compare cmp;
    PAD;

    const K NO_KEY;
    const V NO_VALUE;
    PAD;

    void initThread(const int tid) {
    }
    void deinitThread(const int tid) {
        TM_NS::thread_registry_deregister_thread();
    }

    inline Node<K,V>* createNode(const int tid, const K& key, const V& value, Node<K,V>* const left, Node<K,V>* const right) {
        Node<K,V>* newnode = TM_T::tmNew<Node<K,V>>();
        newnode->key = key;
        newnode->value = value;
        newnode->left = left;
        newnode->right = right;
        return newnode;
    }
    void dfsDeallocateBottomUp(Node<K,V>* u, int *numNodes) {
        if (u == NULL) return;
        if (u->left != NULL) {
            dfsDeallocateBottomUp(u->left, numNodes);
            dfsDeallocateBottomUp(u->right, numNodes);
        }
        const int tid = 0;
        TM_T::tmDelete<Node<K,V>>(u);
    }

    ext_bst_tm(const K _NO_KEY, const V _NO_VALUE, const int numProcesses) : 
        NO_KEY(_NO_KEY), NO_VALUE(_NO_VALUE)
    {
        cmp = Compare();
        const int tid = 0;
        initThread(tid);

        TM_T::updateTx ([&] {
            Node<K,V>* rootleft = createNode(tid, NO_KEY, NO_VALUE, nullptr, nullptr);
            Node<K,V>* _root = createNode(tid, NO_KEY, NO_VALUE, rootleft, nullptr);
            root = _root;
        });
        
        deinitThread(tid);
    }
    ~ext_bst_tm() {
        int numNodes = 0;
        TM_T::updateTx ([&] {
            dfsDeallocateBottomUp(root, &numNodes);
        });
    }


    V insert(const int tid, const K& key, const V& val) { 
        return doInsert(tid, key, val, false); 
    }

    V insertIfAbsent(const int tid, const K& key, const V& val) { 
        return doInsert(tid, key, val, true); 
    }

    bool contains(const int tid, const K& key) { 
        return find(tid, key) != NO_VALUE; 
    }


    V find(const int tid, const K& key) {
        V result = NO_VALUE;
        TM_T::template readTx<V> ([&] {
            Node<K,V>* l = root->left;
            l = l->left;
            if (l == NULL) {
                result = NO_VALUE;
                return result;
            }
            while (l->left) l = (l->key == NO_KEY || cmp(key, l->key)) ? l->left : l->right;
            if (key == l->key) {
                result = l->value;
            }
            else {
                result = NO_VALUE;
            }
            return result;
        });
        return result;
    }

    V doInsert(const int tid, const K& key, const V& val, bool onlyIfAbsent) {
        V result = NO_VALUE;
        TM_T::template updateTx<V> ([&] {
            Node<K,V>* p = root;
            Node<K,V>* l = p->left;
            while (l->left) {
                p = l;
                l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;
            }
            // if we find the key in the tree already
            if (key == l->key) {
                result = l->value;
                if (!onlyIfAbsent) {
                    l->value = val;
                }
                return result;
            } else {
                Node<K,V>* newLeaf = createNode(tid, key, val, NULL, NULL);
                Node<K,V>* newParent = (l->key == NO_KEY || cmp(key, l->key))
                    ? createNode(tid, l->key, l->value, newLeaf, l)
                    : createNode(tid, key, val, l, newLeaf);

                (l == p->left ? p->left : p->right) = newParent;

                result = NO_VALUE;
                return result;
            }
        });
        return result;
    }

    V erase(const int tid, const K& key) {
        V result = NO_VALUE;
        TM_T::template updateTx<V> ([&] {
            Node<K,V>* gp = root;
            Node<K,V>* p = gp->left;
            Node<K,V>* l = p->left;
            if (l == NULL) { // tree is empty
                result = NO_VALUE;
                return NO_VALUE;
            }
            while (l->left) {
                gp = p;
                p = l;
                l = (p->key == NO_KEY || cmp(key, p->key)) ? p->left : p->right;
            }
            // if we fail to find the key in the tree
            if (key != l->key) {
                result = NO_VALUE;
                return NO_VALUE;
            } else {
                result = l->value;
                Node<K,V>* s = (l == p->left ? p->right : p->left);
                (p == gp->left ? gp->left : gp->right) = s;
                TM_T::tmDelete<Node<K,V>>(p);
                TM_T::tmDelete<Node<K,V>>(l);
                return result;
            }
        });
        return result;
    }


    int rangeQuery(const K& low, const K& hi, K * const resultKeys, void ** const resultValues) {
        int result = 0;
        std::vector<Node<K,V>*> stack;

        // depth first traversal (of interesting subtrees)
        TM_T::readTx ([&] {
            stack.clear();
            result = 0;

            stack.push_back(root);
            while (!stack.empty()) {
                Node<K,V>* node = stack.back();
                stack.pop_back();
                Node<K,V>* left = node->left;

                // if internal node, explore its children
                K key = node->key;
                if (left != NULL) {
                    if (key != NO_KEY && !cmp(hi, key)) {
                        stack.push_back(node->right);
                    }
                    if (key == NO_KEY || cmp(low, key)) {
                        stack.push_back(left);
                    }

                // else if leaf node, check if we should return it
                } else {
                    if (key != NO_KEY && !cmp(key, low) && !cmp(hi, key)) {
                        V v = node->value;
                        // node->value = (V) ((uintptr_t) v + 1);
                        resultKeys[result] = key;
                        resultValues[result] = v;
                        ++result;
                    }
                }
            }
        });
        return result;
    }
};




