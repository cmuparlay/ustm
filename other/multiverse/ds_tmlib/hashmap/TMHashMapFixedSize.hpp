//Hashmap implementation from authors of Trinity/Quadra
//included for comparison

#pragma once

#include <string>
#include <cassert>
#include <unordered_set>
#include "tm.h"

template<typename K, typename V>
struct Node {
    tx_field<SAFE,K>        key;
    tx_field<SAFE,V>        val;
    tx_field<SAFE,Node*>    next;
    tx_field<SAFE,uint64_t> isActive;
    Node() = default;
    Node(const K& k, const V& value) : key{k}, val{value}, next {nullptr}, isActive {1} { }
};

#define nodeptr Node<K,V>*

template<class RecordManager, typename K, typename V>
class TMHashMapFixedSize {
public:

    alignas(128) tx_field<SAFE,nodeptr>* buckets;      // An array of pointers to Nodes

    const uint64_t capacity;
    const V NO_VAL;
    PAD;
    RecordManager * recmgr;
    PAD;

    // The default size is hard-coded to 1024 entries in the buckets array
    TMHashMapFixedSize(const int numThreads, V NO_VALUE, int capa=1000000) : capacity(capa), NO_VAL(NO_VALUE) {
        recmgr = new RecordManager(numThreads);
        buckets = (tx_field<SAFE,nodeptr>*)malloc(capacity*sizeof(tx_field<SAFE,nodeptr>));
        std::memset(buckets, 0, capacity*sizeof(tx_field<SAFE,nodeptr>));
        // initThread(0);
        // TM_BEGIN();
        //     for (int i = 0; i < capacity; i++) {
        //         buckets[i] = nullptr;
        //     }
	    // TM_END();
        // deinitThread(0);
    }

    ~TMHashMapFixedSize() {
        // TM_BEGIN();
        //     for(int i = 0; i < capacity; i++){
        //         nodeptr node = buckets[i];
        //         while (node!=nullptr) {
        //             nodeptr next = node->next;
        //             recmgr->deallocate(0, node);
        //             node = next;
        //         }
        //     }
        //     free(buckets);
        // TM_END();
    }

    void initThread(const int tid) {
        recmgr->initThread(tid);
        TM_ARGDECL_ALONE = __tm.TM_ARG_ALONE;
    }

    void deinitThread(const int tid) {
        recmgr->deinitThread(tid);
    }


    /*
     * Adds a node with a key if the key is not present, otherwise replaces the value.
     * Returns the previous value (nullptr by default).
     */
    bool innerPut(const int tid, const K& key, const V& value, V& oldValue, const bool saveOldValue, const bool replaceOldValue) {
        auto h = std::hash<K>{}(key) % capacity;
        nodeptr node = buckets[h];
        nodeptr prev = node;
        nodeptr replnode = nullptr;
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
                nodeptr newnode = recmgr->template allocate<Node<K,V>>(tid); 
                newnode->key = key;
                newnode->val = value;
                newnode->isActive = 1;
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
        nodeptr node = buckets[h];
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
        nodeptr node = buckets[h];
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

    V insert(const int tid, const K& key, const V& val) {        
        auto guard = recmgr->getGuard(tid);
        V retVal = NO_VAL;
        TM_BEGIN();
            retVal = NO_VAL;
            innerPut(tid, key, val, retVal, true, true);
        TM_END();
        return retVal;
    }

    V insertIfAbsent(const int tid, const K& key, const V& val) {
        auto guard = recmgr->getGuard(tid);
        V retVal = NO_VAL;
        TM_BEGIN();
            retVal = NO_VAL;
            innerPut(tid, key, val, retVal, true, false);
        TM_END();
        return retVal;
    }

    V remove(const int tid, const K& key) {
        auto guard = recmgr->getGuard(tid);
        V retVal = NO_VAL;
        TM_BEGIN();
            retVal = NO_VAL;
            innerRemove(key,retVal,true);
        TM_END();
        return retVal;
    }

    bool contains(const int tid,const K& key) {
        auto guard = recmgr->getGuard(tid, true);
        bool retval = false;
        TM_BEGIN_RO();
            V notused;
            retval = innerGet(key,notused,false);
        TM_END();
        return retval;
    }

    V find(const int tid, const K& key) {
        auto guard = recmgr->getGuard(tid, true);
        V retVal = NO_VAL;
        TM_BEGIN_RO();
            retVal = NO_VAL;
            innerGet(key,retVal,true);
        TM_END();
        return retVal;
    } 

    int atomicSize(const int tid) {
        auto guard = recmgr->getGuard(tid, true);
        int count = 0;
        TM_BEGIN_RO();
            count = 0;
            for(int i = 0; i < capacity; i++){
                nodeptr node = buckets[i];
                while (node!=nullptr) {
                    if (node->isActive) {
                        count++;
                    }
                    node = node->next;
                }
            }
        TM_END();
        return count;
    }

    // int rangeQuery(const K& low, const K& hi, K * const resultKeys, void ** const resultValues) {
    //     int count = 0;
    //     auto start = std::hash<K>{}(low) % capacity;
    //     int size = hi - low;
    //     for (int i = 0; i < size; i++) {
    //         int b = (start + i) % capacity;
    //         nodeptr node = buckets[b];
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
