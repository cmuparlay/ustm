#ifndef FUSE_HASHTABLE_H
#define FUSE_HASHTABLE_H

namespace fuse {
#include "../../structures/hash_block/unordered_map.h"
  template <typename K_,
            typename V_,
            class Hash = std::hash<K_>,
            class KeyEqual = std::equal_to<K_>>
  struct tlf_hashtable_map {
    using T = verlib::hash_block<K_,V_,Hash,KeyEqual>;
    using K = typename T::K;
    using V = typename T::V;
    T tr;
    tlf_hashtable_map(long n) : tr(T(n)) {}
    std::optional<V> find(const K& k) {return tr.find_locked(k);}
    bool insert(const K& k , const V& v) {return tr.insert(k,v);}
    bool remove(const K& k) {return tr.remove(k);}
    long size() {return tr.size();}
  };
}
#endif
