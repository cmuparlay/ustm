//Hashmap implementation from authors of Trinity/Quadra
//included for comparison

#pragma once

#include <string>
#include <cassert>

template<typename K, typename V, template <typename> class TMTYPE>
class TMHashMapFixedSize {

private:
    struct Node {
        TMTYPE<K>        key;
        TMTYPE<V>        val;
        TMTYPE<Node*>    next;
        TMTYPE<uint64_t> isActive;
        Node(const K& k, const V& value) { 
            key.store(k);
            val.store(value);
            next.store(nullptr);
            isActive.store(1);
        }
    };



    alignas(128) TMTYPE<TMTYPE<Node*>*> buckets;      // An array of pointers to Nodes
public:
    TMTYPE<uint64_t>                        capacity;

    // The default size is hard-coded to 1024 entries in the buckets array
    TMHashMapFixedSize(int capa=1000000) {
        capacity = capa;
        buckets = (TMTYPE<Node*>*)pmalloc(capa*sizeof(TMTYPE<Node*>));
        for (int i = 0; i < capa; i++) {
            buckets[i] = nullptr;       
            // printf("%p\n", &buckets[i]);     
        }
    }

    ~TMHashMapFixedSize() {
        TM_BEGIN();
            for(int i = 0; i < capacity; i++){
                Node* node = buckets[i];
                while (node!=nullptr) {
                    Node* next = node->next;
                    tmDelete(node);
                    node = next;
                }
            }
            pfree(buckets);
        TM_END();
    }


    /*
     * Adds a node with a key if the key is not present, otherwise replaces the value.
     * Returns the previous value (nullptr by default).
     */
    bool innerPut(const K& key, const V& value, V& oldValue, const bool saveOldValue) {
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
                Node* newnode = tmNew<Node>(key,value);
                if (node == prev) {
                    buckets[h] = newnode;
                } else {
                    prev->next = newnode;
                }
                return true;
            }
            if (key == node->key && node->isActive == 1) {
                if (saveOldValue) oldValue = node->val; // Makes a copy of V
                node->val = value;
                return false; // Replace value for existing key
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

    bool add(const K& key, const V& val) {    
        V notused;
        return innerPut(key,val,notused,false);        
    }

    bool remove(const K& key) {        
        V notused;
        return innerRemove(key,notused,false);        
    }

    bool contains(const K& key) {
        V notused;
        return innerGet(key,notused,false);        
    }
};
