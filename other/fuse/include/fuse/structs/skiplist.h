#ifndef FUSE_SKIPLIST_H
#define FUSE_SKIPLIST_H
  
namespace fuse {
#include "../../structures/skiplist/ordered_map.h"
  template <typename K_,
            typename V_,
            typename Compare = std::less<K_>>
  struct tlf_skiplist_map {
    using T = verlib::skiplist<K_,V_,Compare>;
    using K = typename T::K;
    using V = typename T::V;
    T tr;
    tlf_skiplist_map(long n) : tr(T(n)) {}
    std::optional<V> find(const K& k) {return tr.find_locked(k);}
    bool insert(const K& k , const V& v) {return tr.insert(k,v);}
    bool remove(const K& k) {return tr.remove(k);}
    long size() {return tr.size();}
  };
}

#endif
