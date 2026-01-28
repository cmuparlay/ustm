//Hashmap implementation from authors of Trinity/Quadra
//included for comparison

#pragma once

#include <string>
#include <cassert>
#include <unordered_set>

#include "tm.h"

template<typename K, typename V>
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
    const V NO_VAL;

    // The default size is hard-coded to 1024 entries in the buckets array
    TMHashMapFixedSize(V NO_VALUE, int capa=1000000) : capacity(capa), NO_VAL(NO_VALUE) {
        buckets = (TM_TYPE_T<Node*>*)malloc(capacity*sizeof(TM_TYPE_T<Node*>));
        std::memset(buckets, 0, capacity*sizeof(TM_TYPE_T<Node*>));
        // TM_T::template updateTx<bool>([&] () {
        //     for (int i = 0; i < capacity; i++) {
        //         buckets[i] = nullptr;
        //     }
        //     return true;
	    // });
        // deinitThread();
    }

    ~TMHashMapFixedSize() {
        // TM_T::template updateTx<bool>([&] () {
        //     for(int i = 0; i < capacity; i++){
        //         Node* node = buckets[i];
        //         while (node!=nullptr) {
        //             Node* next = node->next;
        //             TM_T::tmDelete(node);
        //             node = next;
        //         }
        //     }
        //     free(buckets);
        //     return true;
        // });
    }

    void initThread() {
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
            if (key == node->key && node->isActive == 1) {
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
            if (key == node->key && node->isActive == 1) {
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
            if (key == node->key && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                return true;
            }
            node = node->next;
        }
    }

    //
    // Set methods for running the usual tests and benchmarks
    //

    V insert(const K& key, const V& val) {
        V retVal = NO_VAL;
        TM_T::updateTx([&] () {
            retVal = NO_VAL;
            innerPut(key, val, retVal, true, true);
        });
        return retVal;
    }

    V insertIfAbsent(const K& key, const V& val) {
        V retVal = NO_VAL;
        TM_T::updateTx([&] () {
            retVal = NO_VAL;
            innerPut(key, val, retVal, true, false);
        });
        return retVal;
    }

    V remove(const K& key) {
        V retVal = NO_VAL;
        TM_T::updateTx([&] () {
            retVal = NO_VAL;
            innerRemove(key,retVal,true);
        });
        return retVal;
    }

    bool contains(const K& key) {
        bool retval = false;
        TM_T::readTx([&] () {
            V notused;
            retval = innerGet(key,notused,false);
        });
        return retval;
    }

    V find(const K& key) {
        V retVal = NO_VAL;
        TM_T::readTx([&] () {
            retVal = NO_VAL;
            innerGet(key,retVal,true);
        });
        return retVal;
    } 

    int atomicSize() {
        int count = 0;
        TM_T::readTx([&] () {
            count = 0;
            for(int i = 0; i < capacity; i++){
                Node* node = buckets[i];
                while (node!=nullptr) {
                    if (node->isActive) {
                        count++;
                    }
                    node = node->next;
                }
            }
        });
        return count;
    }

    // int rangeQuery(const K& low, const K& hi, K * const resultKeys, void ** const resultValues) {
    //     int count = 0;
    //     auto start = std::hash<K>{}(low) % capacity;
    //     int size = hi - low;
    //     for (int i = 0; i < size; i++) {
    //         int b = (start + i) % capacity;
    //         Node* node = buckets[b];
    //         while (node != nullptr) {
    //             if (node->isActive) {
    //                 count++;
    //             }
    //         }
    //     }
    // }


    bool validate() {
        return true;
    }
};
