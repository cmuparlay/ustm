/*
 * B-tree set (C++)
 *
 * Copyright (c) 2018 Project Nayuki. (MIT License)
 * https://www.nayuki.io/page/btree-set
 *
 * Modified by Andreia Correia
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of
 * the Software, and to permit persons to whom the Software is furnished to do so,
 * subject to the following conditions:
 * - The above copyright notice and this permission notice shall be included in
 *   all copies or substantial portions of the Software.
 * - The Software is provided "as is", without warranty of any kind, express or
 *   implied, including but not limited to the warranties of merchantability,
 *   fitness for a particular purpose and noninfringement. In no event shall the
 *   authors or copyright holders be liable for any claim, damages or other
 *   liability, whether in an action of contract, tort or otherwise, arising from,
 *   out of or in connection with the Software or the use or other dealings in the
 *   Software.
 */

#ifndef _TM_BTREE_H_
#define _TM_BTREE_H_

#pragma once

#include <algorithm>
#include <cassert>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>


#include "ptm.h"

#define MAX_FREE_COUNT 4096
thread_local void* toFree[MAX_FREE_COUNT];
thread_local int toFreeCount = 0;
extern thread_local bool tm_alloc_flag;

#define nodeptr Node<K>*

#define MAXKEYS 15
#define DEGREE 8

using SearchResult = std::pair<bool,int32_t>;

template <typename K, tx_safety mode = SAFE>
class Node final {

	/*-- Fields --*/

	public: tx_field<mode, int32_t> length;
	// Size is in the range [0, maxKeys] for root node, [minKeys, maxKeys] for all other nodes.
	public: tx_field<mode, K>       keys[MAXKEYS];
	// If leaf then size is 0, otherwise if internal node then size always equals keys.size()+1.
	public: tx_field<mode, nodeptr>   children[MAXKEYS+1];

	/*-- Constructor --*/

	public: Node() {}

	// Note: Once created, a node's structure never changes between a leaf and internal node.
	public: Node(uint32_t maxKeys, bool leaf) {
		assert(maxKeys >= 3 && maxKeys % 2 == 1);
		assert(maxKeys <= MAXKEYS);
		length = 0;
		for (int i=0; i < maxKeys+1; i++) children[i] = nullptr;
	}

	/*-- Methods for getting info --*/

	public: bool isLeaf() {
		nodeptr child = children[0];
		return (child==nullptr);
	}

	// Searches this node's keys vector and returns (true, i) if obj equals keys[i],
	// otherwise returns (false, i) if children[i] should be explored. For simplicity,
	// the implementation uses linear search. It's possible to replace it with binary search for speed.
	public: SearchResult search(K &val) {
		int32_t i = 0;
		while (i < this->length) {
			K elem = keys[i];
			if (val == elem) {
				assert(i < this->length);
				return SearchResult(true, i);  // Key found
			} else if (val > elem)
				i++;
			else  // val < elem
				break;
		}
		assert(i <= length);
		return SearchResult(false, i);  // Not found, caller should recurse on child
	}


	/*-- Methods for removal --*/

	// Performs modifications to ensure that this node's child at the given index has at least
	// minKeys+1 keys in preparation for a single removal. The child may gain a key and subchild
	// from its sibling, or it may be merged with a sibling, or nothing needs to be done.
	// A reference to the appropriate child is returned, which is helpful if the old child no longer exists.
	public: nodeptr ensureChildRemove(int32_t minKeys, uint32_t index) {
		// Preliminaries
		assert(!this->isLeaf() && index < this->length+1);
		nodeptr child = this->children[index];
		if (child->length > minKeys)  // Already satisfies the condition
			return child;
		assert(child->length == minKeys);

		// Get siblings
		nodeptr left = index >= 1 ? this->children[index - 1].load() : nullptr;
		nodeptr right = index < this->length ? this->children[index + 1].load() : nullptr;
		bool internal = !child->isLeaf();
		assert(left != nullptr || right != nullptr);  // At least one sibling exists because DEGREE >= 2
		assert(left  == nullptr || left ->isLeaf() != internal);  // Sibling must be same type (internal/leaf) as child
		assert(right == nullptr || right->isLeaf() != internal);  // Sibling must be same type (internal/leaf) as child

		if (left != nullptr && left->length > minKeys) {  // Steal rightmost item from left sibling
			//std::cout<<"Passou left\n";
			if (internal) {
				for(int i=child->length+1;i>=1;i--){
					child->children[i] = child->children[i-1].load();
				}
				child->children[0] = left->children[left->length].load();
			}
			for(int i=child->length;i>=1;i--){
				child->keys[i] = child->keys[i-1].load();
			}
			child->keys[0] = this->keys[index - 1].load();
			this->keys[index-1] = left->keys[left->length-1].load();
			left->length = left->length.load()-1;
			child->length = child->length.load()+1;
			return child;
		} else if (right != nullptr && right->length > minKeys) {  // Steal leftmost item from right sibling
			//std::cout<<"Passou right\n";
			if (internal) {
				child->children[child->length+1] = right->children[0].load();
				for(int i=0;i<right->length;i++){
					right->children[i] = right->children[i+1].load();
				}
			}
			child->keys[child->length] = this->keys[index].load();
			this->keys[index] = right->removeKey(0);
			child->length = child->length.load()+1;
			return child;
		} else if (left != nullptr) {  // Merge child into left sibling
			this->mergeChildren(minKeys, index - 1);
			return left;  // This is the only case where the return value is different
		} else if (right != nullptr) {  // Merge right sibling into child
			this->mergeChildren(minKeys, index);
			return child;
		} else
			throw std::logic_error("Impossible condition");
	}


	// Merges the child node at index+1 into the child node at index,
	// assuming the current node is not empty and both children have minKeys.
	public: void mergeChildren(int32_t minKeys, uint32_t index) {
		assert(!this->isLeaf() && index < this->length);
		nodeptr left  = this->children[index];
		nodeptr right = this->children[index+1];
		assert(left->length == minKeys && right->length == minKeys);
		//std::cout<<"Passou\n";
		if (!left->isLeaf()){
			for(int i=0;i<right->length+1;i++){
				left->children[left->length+1+i] = right->children[i].load();
			}
		}
		left->keys[left->length] = this->keys[index].load();
		for(int i=0;i<right->length;i++){
			left->keys[left->length+1+i] = right->keys[i].load();
		}
		left->length = left->length + right->length.load()+1;
		// remove key(index)
		for(int i=index;i<length-1;i++){
			this->keys[i]=this->keys[i+1].load();
		}
		// remove children (index+1)
		// TM::template tmDelete<Node>(children[index+1]);		
		if (toFreeCount < MAX_FREE_COUNT) {
			toFree[toFreeCount] = children[index+1].load();
			toFreeCount++;
		}
		else {
			printf("Tried to free but no space in buffer\n");
		}

		for(int i=index+1;i<length;i++){
			this->children[i]=this->children[i+1].load();
		}
		this->children[length]=nullptr;
		this->length = this->length.load()-1;
	}


	// Removes and returns the minimum key among the whole subtree rooted at this node.
	// Requires this node to be preprocessed to have at least minKeys+1 keys.
	public: K removeMin(int32_t minKeys) {
		for (nodeptr node = this; ; ) {
			assert(node->length > minKeys);
			if (node->isLeaf()){
				K ret = node->keys[0];
				for(int i=0;i<node->length-1;i++){
					node->keys[i]=node->keys[i+1].load();
				}
				node->length = node->length.load()-1;
				return ret;
			}else{
				node = node->ensureChildRemove(minKeys, 0);
			}

		}
	}


	// Removes and returns the maximum key among the whole subtree rooted at this node.
	// Requires this node to be preprocessed to have at least minKeys+1 keys.
	public: K removeMax(int32_t minKeys) {
		for (Node *node = this; ; ) {
			assert(node->length > minKeys);
			if (node->isLeaf()){
				node->length = node->length.load()-1;
				return node->keys[node->length].load();
			}else{
				node = node->ensureChildRemove(minKeys, node->length);
			}
		}
	}


	// Removes and returns this node's key at the given index.
	public: K removeKey(uint32_t index) {
		K ret = this->keys[index];
		for(int i=index;i < this->length-1;i++){
			this->keys[i] = this->keys[i+1].load();
		}
		this->length = this->length.load()-1;
		return ret;
	}
};


template <typename K, typename V, class RecordManager, tx_safety mode = SAFE>
class TMBTree {
	/*---- Fields ----*/

	private: tx_field<mode, nodeptr>   root;
	private: tx_field<mode, int32_t> minKeys;  // At least 1, equal to DEGREE-1
	private: tx_field<mode, int32_t> maxKeys;  // At least 3, odd number, equal to minKeys*2+1
	
	RecordManager* recmgr;


	/*---- Constructors ----*/

	// The DEGREE is the minimum number of children each non-root internal node must have.
	public: explicit TMBTree(int numThreads) { 
		recmgr = new RecordManager(numThreads);

		initThread(0);
        TM_BEGIN();
		minKeys = DEGREE - 1;
		maxKeys = DEGREE <= UINT32_MAX / 2 ? DEGREE * 2 - 1 : 0; 
		root = nullptr;
		TM_END();
		if (DEGREE < 2)
			throw std::domain_error("DEGREE must be at least 2");
		if (DEGREE > UINT32_MAX / 2)  // In other words, need maxChildren <= UINT32_MAX
			throw std::domain_error("DEGREE too large");
		TM_BEGIN();
		clear(0);
		TM_END();
	}

    ~TMBTree() {
        // deleteAll(root);
    }


	/*---- Methods ----*/

	// public: void deleteAll(nodeptr node){
	// 	if(!node->isLeaf()){
    //         for(int i=0;i<node->length;i++){
    //             deleteAll(node->children[i]);
    //         }
	// 	}
	// 	// TM::template tmDelete<Node>(node);	
	// }


	public: void clear(int tid) {
		// if(root!=nullptr){
		// 	deleteAll(root);
		// }

		// root = TM::template tmNew<Node>(maxKeys, true);
		// root = new Node(maxKeys, true);
		root = recmgr->template allocate<Node<K,mode>>(tid);
		new (root) Node<K,mode>(maxKeys, true);
	}

	/*-- Methods for insertion --*/

	// For the child node at the given index, this moves the right half of keys and children to a new node,
	// and adds the middle key and new child to this node. The left half of child's data is not moved.
	public: void splitChild(int tid, nodeptr node, size_t minKeys, int32_t maxKeys, size_t index) {
		assert(!node->isLeaf() && index <= node->length && node->length < maxKeys);
		nodeptr left = node->children[index];
		// nodeptr right = TM::template tmNew<Node>(maxKeys,left->isLeaf());
		// nodeptr right = new Node(maxKeys,left->isLeaf());
		nodeptr right = recmgr->template allocate<Node<K,mode>>(tid);
		new (right) Node<K,mode>(maxKeys,left->isLeaf());

		if (right == nullptr) {
			printf("ERROR: failed to allocate node\n");
			exit(-1);
		}

		// Handle keys
		K k;
		int j=0;
		for(int i=minKeys + 1;i<left->length;i++){				
			right->keys[j] = left->keys[i].load();
			j++;
		}

		//add right node to this node
		for(int i=node->length+1; i>=index+2;i--){				
			node->children[i]= node->children[i-1].load();
		}
		node->children[index+1] = right;
		for(int i=node->length; i>=index+1;i--){				
			node->keys[i]= node->keys[i-1].load();
		}			
		node->keys[index] = left->keys[minKeys].load();			
		node->length = node->length.load()+1;

		if(!left->isLeaf()){
			j=0;
			for(int i= minKeys + 1;i<left->length+1;i++){
				right->children[j] = left->children[i].load();
				j++;
			}
		}

		right->length = left->length.load()-minKeys-1;
		left->length = minKeys;
	}



	public: bool seqContains(K &val) {
		// Walk down the tree
		nodeptr node = root;
		while (true) {
			SearchResult sr = node->search(val);
			if (sr.first)
				return true;
			else if (node->isLeaf())
				return false;
			else  // Internal node
				node = node->children[sr.second];
		}
	}


	public: bool insert(int tid, K& val) {
		// Special preprocessing to split root node
		if (root->length == maxKeys) {
			nodeptr child = root;
			// root = TM::template tmNew<Node>(maxKeys, false);  // Increment tree height
			// root = new Node(maxKeys, false);
			root = recmgr->template allocate<Node<K,mode>>(tid);
			new (root) Node<K,mode>(maxKeys, false);

			root->children[0] = child;
			// root->splitChild(tid, minKeys, maxKeys, 0);
			splitChild(tid, root, minKeys, maxKeys, 0);
		}

		// Walk down the tree
		nodeptr node = root;
		//std::cout<<root<<" root insert\n";
		//std::cout<<val<<" Key insert\n";
		while (true) {
			// Search for index in current node
			assert(node->length < maxKeys);
			assert(node == root || node->length >= minKeys);

			SearchResult sr = node->search(val);
			if (sr.first)
				return false;  // Key already exists in tree
			int32_t index = sr.second;

			//std::cout<<node<<" insert\n";
			//std::cout<<node->length<<" insert length\n";
			//std::cout<<index<<" insert index\n";
			if (node->isLeaf()) {  // Simple insertion into leaf
				for(int i=node->length;i>=index+1;i--){
					//std::cout<<i<<" i\n";
					//std::cout<<(node->length-1)<<" node->length-1\n";
					//std::cout<<(index+1)<<" index+1\n";
					K k = node->keys[i-1];
					node->keys[i]=k;
				}
				node->keys[index] = val;
				node->length = node->length+1;
				return true;  // Successfully inserted

			} else {  // Handle internal node
				nodeptr child = node->children[index];
				if (child->length == maxKeys) {  // Split child node
					// node->splitChild(tid, minKeys, maxKeys, index);
					splitChild(tid, node, minKeys, maxKeys, index);
					K middleKey = node->keys[index];
					if (val == middleKey)
						return false;  // Key already exists in tree
					else if (val > middleKey)
						child = node->children[index + 1];
				}
				node = child;
			}
		}
	}


	// returns 1 when successul removal
	public: size_t erase(K& val) {
		// Walk down the tree
		bool found;
		int32_t index;
		{
			SearchResult sr = root->search(val);
			found = sr.first;
			index = sr.second;
		}
		nodeptr node = root;
		//std::cout<<val<<" key erase\n";
		while (true) {
			assert(node->length <= maxKeys);
			assert(node == root || node->length > minKeys);
			//std::cout<<node<<" erase\n";
			if (node->isLeaf()) {
				if (found) {  // Simple removal from leaf
					node->removeKey(index);
					return 1;
				} else
					return 0;

			} else {  // Internal node
				if (found) {  // Key is stored at current node
					nodeptr left  = node->children[index + 0];
					nodeptr right = node->children[index + 1];
					assert(left != nullptr && right != nullptr);
					if (left->length > minKeys) {  // Replace key with predecessor
						node->keys[index] = left->removeMax(minKeys);
						return 1;
					} else if (right->length > minKeys) {  // Replace key with successor
						node->keys[index] = right->removeMin(minKeys);
						return 1;
					} else {  // Merge key and right node into left node, then recurse
						node->mergeChildren(minKeys, index);
						if (node == root && root->length==0) {
							assert(root->length+1 == 1);
							nodeptr next = root->children[0];
							// TM::template tmDelete<Node>(root);							
							if (toFreeCount < MAX_FREE_COUNT) {
								toFree[toFreeCount] = root.load();
								toFreeCount++;
							}
							else {
								printf("Tried to free but no space in buffer\n");
							}

							root = next; //TODO:delete root
							//root->children[0] = nullptr;
						}
						node = left;
						index = minKeys;  // Index known due to merging; no need to search
					}

				} else {  // Key might be found in some child
					nodeptr child = node->ensureChildRemove(minKeys, index);
					if (node == root && root->length==0) {
						assert(root->length +1 == 1);
						nodeptr next = root->children[0];
						// TM::template tmDelete<Node>(root);						
						if (toFreeCount < MAX_FREE_COUNT) {
							toFree[toFreeCount] = root.load();
							toFreeCount++;
						}
						else {
							printf("Tried to free but no space in buffer\n");
						}

						root = next; //TODO:delete root
						//root->children[0] = nullptr;
					}
					node = child;
					SearchResult sr = node->search(val);
					found = sr.first;
					index = sr.second;
				}
			}
		}
	}

	// For debugging
	public: void printStructure(nodeptr node) const {
	    if (node == nullptr) return;
	    printf("%p keys = [ ", node);
	    for (int i = 0; i < node->length; i++) printf("%d ", node->keys[i]);
	    printf("]\n");
	    if (node->isLeaf()) return;
	    for (int i = 0; i < node->length+1; i++) printStructure(node->children[i]);
	}


	/*---- Helper class: B-tree node ----*/

public:
    void initThread(const int tid) {
        TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
        recmgr->initThread(tid);
    }
    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }

	void tryFree(int tid) {
		tm_alloc_flag = true;
		for (int i = 0; i < toFreeCount; i++) {
            recmgr->retire(tid, (nodeptr)toFree[i]);                        
		}
		tm_alloc_flag = false;
		toFreeCount = 0;
	}

    // Inserts a key only if it's not already present
    bool add(K key, const int tid=0) {
        bool retval = false;
		TM_BEGIN();        
        retval = insert(tid, key);
        TM_END();
		tryFree(tid);
		return retval;
    }

    // Returns true only if the key was present
    bool remove(K key, const int tid=0) {
        bool retval = false;
        TM_BEGIN();        
        retval = (erase(key) == 1);
        TM_END();
		tryFree(tid);
		return retval;
    }

    bool contains(K key, const int tid=0) {
        bool retval = false;
        TM_BEGIN();        
        retval = seqContains(key);
        TM_END();
		tryFree(tid);
        return retval;
    }

    void addAll(K** keys, uint64_t size, const int tid=0) {
        for (uint64_t i = 0; i < size; i++) add(*keys[i], tid);
    }

};
#endif   // _TM_BTREE_BY_REF_H_
