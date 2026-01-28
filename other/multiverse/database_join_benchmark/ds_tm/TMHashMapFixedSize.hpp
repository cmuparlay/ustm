//Hashmap implementation from authors of Trinity/Quadra
//included for comparison

#pragma once

#include <string>
#include <cassert>
#include <unordered_set>
#include "ds_common.h"

template<typename K, typename V, class KeyCompare>
class TMHashMapFixedSize {

private:
    struct Node {
        TM_TYPE_T<K>        key;
        TM_TYPE_T<V>        val;
        TM_TYPE_T<Node*>    next {nullptr};
        TM_TYPE_T<uint64_t> isActive {1};
        Node(const K& k, const V& value) : key{k}, val{value}, next {nullptr}, isActive {1} { }
    };


    alignas(128) TM_TYPE_T<Node*>* buckets;      // An array of pointers to Nodes
public:
    const uint64_t capacity;
    KeyCompare keyCmp;

    // The default size is hard-coded to 1024 entries in the buckets array
    TMHashMapFixedSize(int capa=1000000) : capacity(capa) {
        buckets = (TM_TYPE_T<Node*>*)malloc(capacity*sizeof(TM_TYPE_T<Node*>));
        TM_T::template updateTx<bool>([&] () {            
            for (int i = 0; i < capacity; i++) {
                buckets[i] = nullptr;
            }
            return true;
	    });
        deinitThread();
    }

    ~TMHashMapFixedSize() {
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
    bool innerPut(const K& key, const V& value, V& oldValue, const bool saveOldValue, const bool replaceOldValue) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        Node* prev = node;
        Node* replnode = nullptr;
        V* oldVal = nullptr;
        while (true) {
            if (node == nullptr) {
                if (replnode != nullptr) {
                    // Found a replacement node
                    assert(replnode->isActive == 0);
                    replnode->key = key;
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
            if (keyCmp(key,node->key) == 0 && node->isActive == 1) {
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
    bool innerRemove(const K& key, V& oldValue, const bool saveOldValue) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (keyCmp(key,node->key) == 0 && node->isActive == 1) {
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
    bool innerGet(const K& key, V& oldValue, const bool saveOldValue) {
        auto h = std::hash<K>{}(key) % capacity;
        Node* node = buckets[h];
        while (true) {
            if (node == nullptr) return false;
            if (keyCmp(key,node->key) == 0 && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                return true;
            }
            node = node->next;
        }
    }

    //
    // Set methods for running the usual tests and benchmarks
    //

    void update(const K& key, const V& val) {        
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {    
            retVal = NO_VAL;        
            innerPut(key, val, retVal, true, true);
        });        
    }

    V addValue(const K& key, const V& val) {

    }

    V addKey(const K& key, const V& val) {
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {            
            retVal = NO_VAL;
            innerPut(key, val, retVal, true, false);
        });
        return retVal;
    }

    V removeKey(const K& key) {
        V retVal = NO_VAL;
        TM_T::template updateTx<bool>([&] () {
            retVal = NO_VAL;
            innerRemove(key,retVal,true);
        });
        return retVal;
    }

    bool contains(const K& key) {
        bool retval = false;
        TM_T::template readTx<bool>([&] () {
            V notused;
            retval = innerGet(key,notused,false);
        });
        return retval;
    }

    V get(const K& key) {
        V retVal = NO_VAL;
        TM_T::template readTx<bool>([&] () {            
            retVal = NO_VAL;
            innerGet(key,retVal,true);
        });
        return retVal;
    } 

	void getSuccessorKey(const K& key, K* succcessorKey, V* successorVal){
		auto h = std::hash<K>{}(key) % capacity;
        K succKeyTmp;
        V succValTmp;
        TM_T::template readTx<bool>([&] () {
            Node* node = buckets[h];
            while (!node || node->isActive == 0) {
                h = (h+1) % capacity;
                node = buckets[h];            
            }
            succValTmp = node->val;
            succKeyTmp = node->key;            
        });
        *successorVal = succValTmp;
        *succcessorKey = succKeyTmp;
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
