#ifndef FUSE_ARTTREE_H
#define FUSE_ARTTREE_H
  
namespace fuse {
#include "../../structures/arttree/ordered_map.h"
  template <typename K_,
            typename V_,
            typename String = verlib::int_string<K_>>
  struct tlf_arttree_map {
    using T = verlib::arttree<K_,V_,String>;
    using K = typename T::K;
    using V = typename T::V;
    T tr;
    tlf_arttree_map(long n) : tr(T(n)) {}
    std::optional<V> find(const K& k) {return tr.find_locked(k);}
    bool insert(const K& k , const V& v) {return tr.insert(k,v);}
    bool remove(const K& k) {return tr.remove(k);}
    long size() {return tr.size();}
  };
}

#endif
