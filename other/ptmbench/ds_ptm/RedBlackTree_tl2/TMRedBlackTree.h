/* Original file from RedoDB repo - added here for comparison against our work.

 * Copyright 2017-2020
 *   Andreia Correia <andreia.veiga@unine.ch>
 *   Pedro Ramalhete <pramalhe@gmail.com>
 *   Pascal Felber <pascal.felber@unine.ch>
 *
 * This work is published under the MIT license. See LICENSE.txt
 */

#ifndef _PERSISTENT_TM_RED_BLACK_BST_H_
#define _PERSISTENT_TM_RED_BLACK_BST_H_

#include <cassert>
#include <stdexcept>
#include <algorithm>

#include "ptm.h"
#include "record_manager.h"


#define RBT_COLOR_RED    0L
#define RBT_COLOR_BLACK  1L

#define nodeptr Node<K,V>*

extern thread_local bool tm_alloc_flag;


template<typename K, typename V, tx_safety mode = SAFE>
struct Node {
    tx_field<mode, K>       key;
    tx_field<mode, V>       val;
    tx_field<mode, nodeptr>   left;
    tx_field<mode, nodeptr>   right;
    tx_field<mode, int64_t> color;    // color of parent link
    tx_field<mode, int64_t> size;     // subtree count
    Node(const K& key, const V& val, int64_t color, int64_t size) : key{key}, val{val}, color{color}, size{size} {}
    Node() {}
};

//This is kind of a stupid solution to getting freeing to work 
//mostly because the maximum number of nodes to free need to be very high
#define MAX_TO_FREE 16384 //2^14
thread_local int nodesToFreeCount = 0;
thread_local void* nodesToFree[MAX_TO_FREE];


// Adapted from Java to C++ from the original at http://algs4.cs.princeton.edu/code/edu/princeton/cs/algs4/RedBlackBST.java
template<typename K, typename V, class RecManager, tx_safety mode = SAFE>
class TMRedBlackTree {
    //static constexpr int64_t RBT_COLOR_RED   = 0;
    //static constexpr int64_t COLOR_BLACK = 1;

    tx_field<mode,nodeptr> root;   // root of the BST

    inline void assignAndFreeIfNull(int tid, tx_field<mode,nodeptr>& z, nodeptr w) {
        nodeptr tofree = z.load();
        z = w;
        // if (w == nullptr) TM::tmDelete(tofree);
        if (w == nullptr) {
            if (nodesToFreeCount < MAX_TO_FREE) {
                nodesToFree[nodesToFreeCount] = (void*)(tofree);
                nodesToFreeCount++;
            }
        }
    }

public:
    K resKey;
    V noVal;
    RecManager* recmgr;

    void initThread(const int tid) {
        TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
        recmgr->initThread(tid);
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }


    /**
     * Initializes an empty symbol table.
     */
    TMRedBlackTree(K reservedKey, V noValue, int numThreads){
        resKey = reservedKey;
        noVal = noValue;    
        recmgr = new RecManager(numThreads);
    }

    ~TMRedBlackTree() {
        // The transaction log is not enough to delete everything if there are too many, so we delete 1000 per transaction
        //for (uint64_t i = 0; i < 1000*1000*10ULL; i++) {
            // TM::template updateTx<bool>([=] () {
            //     if (root == nullptr) return true;
            //     deleteAll(root);
            //     return true;
            // });
        //}
    }

    // void deleteAll(nodeptr rt){
    // 	nodeptr left = rt->left;
	// 	if(left!=nullptr){
	// 		deleteAll(left);
	// 	}
	// 	nodeptr right = rt->right;
	// 	if(right!=nullptr){
	// 		deleteAll(right);
	// 	}
	// 	TM::tmDelete(rt);
    // 	return;
    // }

    /***************************************************************************
     *  Node helper methods.
     ***************************************************************************/
    // is node x red; false if x is null ?
    bool isRed(nodeptr x) {
        if (x == nullptr) return false;
        return x->color == RBT_COLOR_RED;
    }

    // number of node in subtree rooted at x; 0 if x is null
    int size(nodeptr x) {
        if (x == nullptr) return 0;
        return x->size;
    }


    /**
     * Returns the number of key-value pairs in this symbol table.
     * @return the number of key-value pairs in this symbol table
     */
    int size() {
        return size(root);
    }

    /**
     * Is this symbol table empty?
     * @return {@code true} if this symbol table is empty and {@code false} otherwise
     */
    bool isEmpty() {
        return root == nullptr;
    }


    /***************************************************************************
     *  Standard BST search->
     ***************************************************************************/

    /**
     * Returns the value associated with the given key.
     * @param key the key
     * @return the value associated with the given key if the key is in the symbol table
     *     and {@code null} if the key is not in the symbol table
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    bool innerGet(int tid, K key, V& oldValue) {
        nodeptr foundNode = get(root, key);
        bool found = false;
        if (foundNode && foundNode->key == key) {
            oldValue = foundNode->val;
            found = true;
        }        
        return found;
    }    

    // value associated with the given key in subtree rooted at x; null if no such key
    nodeptr get(nodeptr x, K& key) {
        while (x != nullptr) {
            if      (key < x->key) x = x->left;
            else if (x->key < key) x = x->right;
            else              return x;
        }
        return nullptr;
    }

    /**
     * Does this symbol table contain the given key?
     * @param key the key
     * @return {@code true} if this symbol table contains {@code key} and
     *     {@code false} otherwise
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    bool containsKey(const K& key) {
        return get(key) != nullptr;
    }

    // This is just a quick hack to get YCSB to run
    V getValue(const K& key) {
        nodeptr x = root;
        while (x != nullptr) {
            if      (key < x->key) x = x->left;
            else if (x->key < key) x = x->right;
            else              return x->val;
        }
        return (V)nullptr;
    }


    /***************************************************************************
     *  Red-black tree insertion.
     ***************************************************************************/

    /**
     * Inserts the specified key-value pair into the symbol table, overwriting the old
     * value with the new value if the symbol table already contains the specified key.
     * Deletes the specified key (and its associated value) from this symbol table
     * if the specified value is {@code null}.
     *
     * @param key the key
     * @param val the value
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    bool innerPut(int tid, const K& key, const V& value) {
    	bool ret = false;
        root = put(tid, root, key, value, ret);
        root->color = RBT_COLOR_BLACK;
        return ret;
    }

    // insert the key-value pair in the subtree rooted at h
    nodeptr put(int tid, nodeptr h, const K& key, const V& val, bool& ret) {
        if (h == nullptr) {
            ret = true;
            nodeptr newNode = recmgr->template allocate<Node<K,V,mode>>(tid);
            // return TM::template tmNew<Node>(key, val, RBT_COLOR_RED, 1);
            //nodeptr newNode = TM::template tmNew<Node>();
            newNode->key = key;
            newNode->val = val;
            newNode->left = nullptr;
            newNode->right = nullptr;
            newNode->color = RBT_COLOR_RED;
            newNode->size = 1;
            return newNode;
        }
        if      (key < h->key) h->left  = put(tid, h->left,  key, val, ret);
        else if (h->key < key) h->right = put(tid, h->right, key, val, ret);
        else              h->val   = val;
        // fix-up any right-leaning links
        if (isRed(h->right) && !isRed(h->left))       h = rotateLeft(h);
        if (isRed(h->left)  &&  isRed(h->left->left)) h = rotateRight(h);
        if (isRed(h->left)  &&  isRed(h->right))      flipColors(h);
        h->size = size(h->left) + size(h->right) + 1;

        return h;
    }

    /***************************************************************************
     *  Red-black tree deletion.
     ***************************************************************************/

    /**
     * Removes the smallest key and associated value from the symbol table.
     * @throws NoSuchElementException if the symbol table is empty
     */
    void deleteMin(int tid) {
        if (isEmpty()) return;
        // if both children of root are black, set root to red
        if (!isRed(root->left) && !isRed(root->right))
            root->color = RBT_COLOR_RED;
        assignAndFreeIfNull(tid, root, deleteMin(tid, root));
        if (!isEmpty()) root->color = RBT_COLOR_BLACK;
        // assert check();
    }

    // delete the key-value pair with the minimum key rooted at h
    nodeptr deleteMin(int tid, nodeptr h) {
        if (h->left == nullptr)
            return nullptr;
        if (!isRed(h->left) && !isRed(h->left->left))
            h = moveRedLeft(h);
        assignAndFreeIfNull(tid, h->left, deleteMin(tid, h->left));
        return balance(h);
    }


    /**
     * Removes the largest key and associated value from the symbol table.
     * @throws NoSuchElementException if the symbol table is empty
     */
    void deleteMax() {
        if (isEmpty()) return;

        // if both children of root are black, set root to red
        if (!isRed(root->left) && !isRed(root->right))
            root->color = RBT_COLOR_RED;

        root = deleteMax(root);
        if (!isEmpty()) root->color = RBT_COLOR_BLACK;
        // assert check();
    }

    // delete the key-value pair with the maximum key rooted at h
    nodeptr deleteMax(nodeptr h) {
        if (isRed(h->left))
            h = rotateRight(h);

        if (h->right == nullptr)
            return nullptr;

        if (!isRed(h->right) && !isRed(h->right->left))
            h = moveRedRight(h);

        h->right = deleteMax(h->right);

        return balance(h);
    }

    /**
     * Removes the specified key and its associated value from this symbol table
     * (if the key is in this symbol table).
     *
     * @param  key the key
     */
    void innerRemove(int tid, K key) {
        // if both children of root are black, set root to red
        if (!isRed(root->left) && !isRed(root->right)) root->color = RBT_COLOR_RED;
        assignAndFreeIfNull(tid, root, deleteKey(tid, root, key));
        if (!isEmpty()) root->color = RBT_COLOR_BLACK;
        // assert check();
    }

    // delete the key-value pair with the given key rooted at h
    nodeptr deleteKey(int tid, nodeptr h, const K& key) {
        // assert get(h, key) != null;
        if (key < h->key)  {
            if (!isRed(h->left) && !isRed(h->left->left)) {
                h = moveRedLeft(h);
            }
            assignAndFreeIfNull(tid, h->left, deleteKey(tid, h->left, key));
        } else {
            if (isRed(h->left)) {
                h = rotateRight(h);
            }
            if (key == h->key && (h->right == nullptr)) {
                return nullptr;
            }
            if (!isRed(h->right) && !isRed(h->right->left)) {
                h = moveRedRight(h);
            }
            if (key == h->key) {
                nodeptr x = min(h->right);
                h->key = x->key.load();
                h->val = x->val.load();
                // h->val = get(h->right, min(h->right).key);
                // h->key = min(h->right).key;
                assignAndFreeIfNull(tid, h->right, deleteMin(tid, h->right));
            } else {
                assignAndFreeIfNull(tid, h->right, deleteKey(tid, h->right, key));
            }
        }
        return balance(h);
    }

    /***************************************************************************
     *  Red-black tree helper functions.
     ***************************************************************************/

    // make a left-leaning link lean to the right
    nodeptr rotateRight(nodeptr h) {
        // assert (h != null) && isRed(h->left);
        nodeptr x = h->left;
        h->left = x->right.load();
        x->right = h;
        x->color = x->right->color.load();
        x->right->color = RBT_COLOR_RED;
        x->size = h->size.load();
        h->size = size(h->left) + size(h->right) + 1;
        return x;
    }

    // make a right-leaning link lean to the left
    nodeptr rotateLeft(nodeptr h) {
        // assert (h != null) && isRed(h->right);
        nodeptr x = h->right;
        h->right = x->left.load();
        x->left = h;
        x->color = x->left->color.load();
        x->left->color = RBT_COLOR_RED;
        x->size = h->size.load();
        h->size = size(h->left) + size(h->right) + 1;
        return x;
    }

    // flip the colors of a node and its two children
    void flipColors(nodeptr h) {
        // h must have opposite color of its two children
        // assert (h != null) && (h->left != null) && (h->right != null);
        // assert (!isRed(h) &&  isRed(h->left) &&  isRed(h->right))
        //    || (isRed(h)  && !isRed(h->left) && !isRed(h->right));
        h->color = !h->color;
        h->left->color = !h->left->color;
        h->right->color = !h->right->color;
    }

    // Assuming that h is red and both h->left and h->left.left
    // are black, make h->left or one of its children red.
    nodeptr moveRedLeft(nodeptr h) {
        // assert (h != null);
        // assert isRed(h) && !isRed(h->left) && !isRed(h->left.left);

        flipColors(h);
        if (isRed(h->right->left)) {
            h->right = rotateRight(h->right);
            h = rotateLeft(h);
            flipColors(h);
        }
        return h;
    }

    // Assuming that h is red and both h->right and h->right.left
    // are black, make h->right or one of its children red.
    nodeptr moveRedRight(nodeptr h) {
        // assert (h != null);
        // assert isRed(h) && !isRed(h->right) && !isRed(h->right.left);
        flipColors(h);
        if (isRed(h->left->left)) {
            h = rotateRight(h);
            flipColors(h);
        }
        return h;
    }

    // restore red-black tree invariant
    nodeptr balance(nodeptr h) {
        // assert (h != null);

        if (isRed(h->right))                        h = rotateLeft(h);
        if (isRed(h->left) && isRed(h->left->left)) h = rotateRight(h);
        if (isRed(h->left) && isRed(h->right))      flipColors(h);

        h->size = size(h->left) + size(h->right) + 1;
        return h;
    }


    /***************************************************************************
     *  Utility functions.
     ***************************************************************************/

    /**
     * Returns the height of the BST (for debugging).
     * @return the height of the BST (a 1-node tree has height 0)
     */
    int height() {
        return height(root);
    }
    int height(nodeptr x) {
        if (x == nullptr) return -1;
        return 1 + std::max(height(x->left), height(x->right));
    }

    /***************************************************************************
     *  Ordered symbol table methods.
     ***************************************************************************/

    /**
     * Returns the smallest key in the symbol table.
     * @return the smallest key in the symbol table
     * @throws NoSuchElementException if the symbol table is empty
     */
    K* min() {
        if (isEmpty()) return nullptr;
        return min(root).key;
    }

    // the smallest key in subtree rooted at x; null if no such key
    nodeptr min(nodeptr x) {
        // assert x != null;
        if (x->left == nullptr) return x;
        else                return min(x->left);
    }

    /**
     * Returns the largest key in the symbol table.
     * @return the largest key in the symbol table
     * @throws NoSuchElementException if the symbol table is empty
     */
    K* max() {
        if (isEmpty()) return nullptr;
        return max(root).key;
    }

    // the largest key in the subtree rooted at x; null if no such key
    nodeptr max(nodeptr x) {
        // assert x != null;
        if (x->right == nullptr) return x;
        else                 return max(x->right);
    }


    /**
     * Returns the largest key in the symbol table less than or equal to {@code key}.
     * @param key the key
     * @return the largest key in the symbol table less than or equal to {@code key}
     * @throws NoSuchElementException if there is no such key
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    K* floor(const K& key) {
        if (key == nullptr) return nullptr;
        if (isEmpty()) return nullptr;
        nodeptr x = floor(root, key);
        if (x == nullptr) return nullptr;
        else           return x->key;
    }

    // the largest key in the subtree rooted at x less than or equal to the given key
    nodeptr floor(nodeptr x, const K& key) {
        if (x == nullptr) return nullptr;
        if (key == x->key) return x;
        if (key < x->key)  return floor(x->left, key);
        nodeptr t = floor(x->right, key);
        if (t != nullptr) return t;
        else           return x;
    }

    /**
     * Returns the smallest key in the symbol table greater than or equal to {@code key}.
     * @param key the key
     * @return the smallest key in the symbol table greater than or equal to {@code key}
     * @throws NoSuchElementException if there is no such key
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    K* ceiling(const K& key) {
        if (key == nullptr) return nullptr;
        if (isEmpty()) return nullptr;
        nodeptr x = ceiling(root, key);
        if (x == nullptr) return nullptr;
        else           return x->key;
    }

    // the smallest key in the subtree rooted at x greater than or equal to the given key
    nodeptr ceiling(nodeptr x, const K& key) {
        if (x == nullptr) return nullptr;
        if (key == x->key) return x;
        if (x->key < key)  return ceiling(x->right, key);
        nodeptr t = ceiling(x->left, key);
        if (t != nullptr) return t;
        else           return x;
    }

    /**
     * Return the kth smallest key in the symbol table.
     * @param k the order statistic
     * @return the {@code k}th smallest key in the symbol table
     * @throws IllegalArgumentException unless {@code k} is between 0 and
     *     <em>n</em>�1
     */
    K* select(int k) {
        if (k < 0 || k >= size()) {
            return nullptr;
        }
        Node x = select(root, k);
        return x->key;
    }

    // the key of rank k in the subtree rooted at x
    nodeptr select(nodeptr x, int k) {
        // assert x != null;
        // assert k >= 0 && k < size(x);
        int t = size(x->left);
        if      (t > k) return select(x->left,  k);
        else if (t < k) return select(x->right, k-t-1);
        else            return x;
    }

    /**
     * Return the number of keys in the symbol table strictly less than {@code key}.
     * @param key the key
     * @return the number of keys in the symbol table strictly less than {@code key}
     * @throws IllegalArgumentException if {@code key} is {@code null}
     */
    int rank(const K& key) {
        if (key == nullptr) return -1;
        return rank(key, root);
    }

    // number of keys less than key in the subtree rooted at x
    int rank(const K& key, nodeptr x) {
        if (x == nullptr) return 0;
        if      (key < x->key) return rank(key, x->left);
        else if (x->key < key) return 1 + size(x->left) + rank(key, x->right);
        else              return size(x->left);
    }

    /***************************************************************************
     *  Range count and range search->
     ***************************************************************************/


    /**
     * Returns the number of keys in the symbol table in the given range.
     *
     * @param  lo minimum endpoint
     * @param  hi maximum endpoint
     * @return the number of keys in the sybol table between {@code lo}
     *    (inclusive) and {@code hi} (inclusive)
     * @throws IllegalArgumentException if either {@code lo} or {@code hi}
     *    is {@code null}
     */
    int size(const K& lo, const K& hi) {
        if (lo == nullptr) return 0;
        if (hi == nullptr) return 0;

        if (hi < lo) return 0;
        if (containsKey(hi)) return rank(hi) - rank(lo) + 1;
        else              return rank(hi) - rank(lo);
    }


    /***************************************************************************
     *  Check integrity of red-black tree data structure.
     ***************************************************************************/
    bool check() {
        if (!isBST())            std::cout << "Not in symmetric order\n";
        if (!isSizeConsistent()) std::cout << "Subtree counts not consistent\n";
        //if (!isRankConsistent()) std::cout << "Ranks not consistent\n";
        if (!is23())             std::cout << "Not a 2-3 tree\n";
        if (!isBalanced())       std::cout << "Not balanced\n";
        return isBST() && isSizeConsistent() && is23() && isBalanced();
    }

    // does this binary tree satisfy symmetric order?
    // Note: this test also ensures that data structure is a binary tree since order is strict
    bool isBST() {
        return isBST(root, nullptr, nullptr);
    }

    // is the tree rooted at x a BST with all keys strictly between min and max
    // (if min or max is null, treat as empty constraint)
    // Credit: Bob Dondero's elegant solution
    bool isBST(nodeptr x, K* min, K* max) {
        if (x == nullptr) return true;
        // TODO: port these two lines
        //if (min != nullptr && x->key.compareTo(min) <= 0) return false;
        //if (max != nullptr && x->key.compareTo(max) >= 0) return false;
        return isBST(x->left, min, x->key) && isBST(x->right, x->key, max);
    }

    // are the size fields correct?
    bool isSizeConsistent() { return isSizeConsistent(root); }
    bool isSizeConsistent(nodeptr x) {
        if (x == nullptr) return true;
        if (x->size != size(x->left) + size(x->right) + 1) return false;
        return isSizeConsistent(x->left) && isSizeConsistent(x->right);
    }

    /*
    // check that ranks are consistent
    bool isRankConsistent() {
        for (int i = 0; i < size(); i++)
            if (i != rank(select(i))) return false;
        for (K* key : keys())
            if (key.compareTo(select(rank(key))) != 0) return false;
        return true;
    }
    */

    // Does the tree have no red right links, and at most one (left)
    // red links in a row on any path?
    bool is23() { return is23(root); }
    bool is23(nodeptr x) {
        if (x == nullptr) return true;
        if (isRed(x->right)) return false;
        if (x != root && isRed(x) && isRed(x->left))
            return false;
        return is23(x->left) && is23(x->right);
    }

    // do all paths from root to leaf have same number of black edges?
    bool isBalanced() {
        int black = 0;     // number of black links on path from root to min
        Node x = root;
        while (x != nullptr) {
            if (!isRed(x)) black++;
            x = x->left;
        }
        return isBalanced(root, black);
    }

    // does every path from the root to a leaf have the given number of black links?
    bool isBalanced(nodeptr x, int black) {
        if (x == nullptr) return black == 0;
        if (!isRed(x)) black--;
        return isBalanced(x->left, black) && isBalanced(x->right, black);
    }

    void tryFree(const int tid) {
        assert(nodesToFreeCount < MAX_TO_FREE);        
        tm_alloc_flag = true;
        for (int i = 0; i < nodesToFreeCount; i++) {            
            recmgr->retire(tid, (nodeptr)nodesToFree[i]);                        
            // recmgr->deallocate(tid, (nodeptr)nodesToFree[i]);            
        }
        tm_alloc_flag = false;
        nodesToFreeCount = 0;
    }

    // Inserts a key only if it's not already present    
    V add(K key, V val, const int tid) {
        auto guard = recmgr->getGuard(tid);
        nodesToFreeCount = 0;
        TM_BEGIN();
        V notused;        
        if (innerGet(tid, key,notused)) {
            TM_END();
            tryFree(tid);            
            return notused;
        }            
        innerPut(tid, key,val);        
        TM_END();
        tryFree(tid);
        return noVal;                
    }

    // Returns true only if the key was present
    V remove(K key, const int tid) {        
        auto guard = recmgr->getGuard(tid);
        nodesToFreeCount = 0;
        TM_BEGIN();
        V notused;
        bool retval = innerGet(tid, key,notused);
        if (retval) {
            innerRemove(tid, key);
            TM_END();
            tryFree(tid);
            return notused;
        }
        TM_END();
        tryFree(tid);
        return noVal;        
    }

    bool contains(K key, const int tid) {
        auto guard = recmgr->getGuard(tid);
        nodesToFreeCount = 0;
        TM_BEGIN();
        V notused;
        bool ret = innerGet(tid, key,notused);
        TM_END();
        return ret;        
    }

    // void addAll(K** keys, int size, const int tid=0) {
    //     for (int i = 0; i < size; i++) add(*keys[i], tid);
    // }

    // static std::string className() { return TM::className() + "-RedBlackTree"; }

};

#endif   // _PERSISTENT_TM_RED_BLACK_BST_H_
