//Hashmap implementation from authors of Trinity/Quadra
//included for comparison

#pragma once

#include <string>
#include <cassert>
#include <unordered_set>
#include "ds_common.h"

template<typename K, int KEY_COUNT, typename V, class KeyCompare>
class TMHashMapFixedSize_MultiColumnKey {

private:
    struct Node {
        TM_TYPE_T<K>        key[KEY_COUNT];
        TM_TYPE_T<V>        val;
        TM_TYPE_T<Node*>    next {nullptr};
        TM_TYPE_T<uint64_t> isActive {1};
        Node(K k[KEY_COUNT], const V& value) : val{value}, next {nullptr}, isActive {1} { 
            for (int i = 0; i < KEY_COUNT; i++) {
                key[i] = k[i];
            }
        }
    };

    int multiColCompare(K* a, TM_TYPE_T<K>* b) {
        for (int i = 0; i < KEY_COUNT; i++) {
            if (a[i] < b[i]) {
                return -1;
            }
            else if (a[i] > b[i]) {
                return 1;
            }
        }
        return 0;
    }


    alignas(128) TM_TYPE_T<Node*>* buckets;      // An array of pointers to Nodes
public:
    const uint64_t capacity;
    KeyCompare keyCmp;

    // The default size is hard-coded to 1024 entries in the buckets array
    TMHashMapFixedSize_MultiColumnKey(int capa=1000000) : capacity(capa) {
		keyCmp = KeyCompare();
        buckets = (TM_TYPE_T<Node*>*)malloc(capacity*sizeof(TM_TYPE_T<Node*>));
        TM_T::template updateTx<bool>([&] () {            
            for (int i = 0; i < capacity; i++) {
                buckets[i] = nullptr;
            }
            return true;
	    });
        deinitThread();
    }

    ~TMHashMapFixedSize_MultiColumnKey() {
        TM_T::template updateTx<bool>([&] () {
            for(int i = 0; i < capacity; i++){
                Node* node = buckets[i];
                while (node!=nullptr) {
                    Node* next = node->next;
                    TM_T::tmDelete(node);
                    node = next;
                }
            }
            free(buckets);
            return true;
        });
    }

    void deinitThread() {
        TM_NS::thread_registry_deregister_thread();
    }

    /*
     * Adds a node with a key if the key is not present, otherwise replaces the value.
     * Returns the previous value (nullptr by default).
     */
    bool innerPut(K key[KEY_COUNT], const V& value, V& oldValue, const bool saveOldValue, const bool replaceOldValue) {
        auto h = std::hash<K>{}((uint64_t)key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        Node* replnode = nullptr;
        V* oldVal = nullptr;
        while (true) {
            if (node == nullptr) {
                if (replnode != nullptr) {
                    // Found a replacement node
                    assert(replnode->isActive == 0);
                    for (int i = 0; i < KEY_COUNT; i++) {
                        replnode->key[i] = key[i]; 
                    }
                    replnode->val = value;
                    replnode->isActive = 1;
                    return true;
                }
                Node* newnode = TM_T::template tmNew<Node>(key,value);
                if (node == prev) {
                    buckets[h] = newnode;
                } else {
                    prev->next = newnode;
                }
                return true;
            }
            if (multiColCompare(key,node->key) == 0 && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                if (replaceOldValue) {                    
                    node->val = value;
                    return true; // Replace value for existing key
                }
                return false;
            }
            if (replnode == nullptr && node->isActive == 0) replnode = node;
            prev = node;
            node = node->next;
        }
    }

    /*
     * Removes a key, returning the value associated with it.
     * Returns nullptr if there is no matching key.
     */
    bool innerRemove(K key[KEY_COUNT], V& oldValue, const bool saveOldValue) {
        auto h = std::hash<K>{}((uint64_t)key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (multiColCompare(key,node->key) == 0 && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                node->isActive = 0; // Instead of tmDelete()
                return true;
            }
            node = node->next;
        }
    }


    /*
     * Returns the value associated with the key, nullptr if there is no mapping
     */
    bool innerGet(K key[KEY_COUNT], V& oldValue, const bool saveOldValue) {
        auto h = std::hash<K>{}((uint64_t)key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (multiColCompare(key,node->key) == 0 && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                return true;
            }
            node = node->next;
        }
    }

    //
    // Set methods for running the usual tests and benchmarks
    //

    void update(K key[KEY_COUNT], const V& val) {        
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {    
            retVal = NO_VAL;        
            innerPut(key, val, retVal, true, true);
        });        
    }

    V addValue(const K& key, const V& val) {

    }

    V addKey(K key[KEY_COUNT], const V& val) {
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {            
            retVal = NO_VAL;
            innerPut(key, val, retVal, true, false);
        });
        return retVal;
    }

    V removeKey(K key[KEY_COUNT]) {
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {
            retVal = NO_VAL;
            innerRemove(key,retVal,true);
        });
        return retVal;
    }

    bool contains(K key[KEY_COUNT]) {
        bool retval = false;
        TM_T::template readTx<bool>([&] () {
            V notused;
            retval = innerGet(key,notused,false);
        });
        return retval;
    }

    V get(K key[KEY_COUNT]) {
        V retVal = NO_VAL;
        TM_T::template readTx<bool>([&] () {            
            retVal = NO_VAL;
            innerGet(key,retVal,true);
        });
        return retVal;
    } 

	void getSuccessorKey(K key[KEY_COUNT], K* successorKey, V* successorVal){
		auto h = std::hash<K>{}((uint64_t)key) % capacity;
        Node* node = buckets[h];
        while (!node || node->isActive == 0) {
            h = (h+1) % capacity;
            node = buckets[h];            
        }
        *successorVal = node->val;
        for (int i = 0; i < KEY_COUNT; i++) {
            successorKey[i] = node->key[i];
        }
        return;
	}

    int rangeQuery(const K& lo, const K& hi, K * const resultKeys, V * const resultValues) {
        printf("Hashmap does not support range query\n");
        exit(-1);
    }

    bool validate() {
        bool retVal = true;
        std::unordered_set<K> keys;
        TM_T::template readTx<bool>([&] () {
            keys.clear();
            for (int i = 0; i < capacity; i++) {
                Node* curr = buckets[i];
                while (curr) {
                    if (curr->isActive == 1) {                        
                        K key = curr->key;
                        if (keys.count(key) > 0) {
                            retVal = false;
                            return false;
                        }
                        keys.insert(key);
                    }
                    curr = curr->next;
                }                
            }
            return true;
        });
        return retVal;
    }
};
