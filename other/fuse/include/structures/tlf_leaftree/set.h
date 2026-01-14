#include "ordered_map.h"
template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
  using T = fuse::tlf_leaftree_map<K_,V_,Compare>;
  using K = typename T::K;
  using V = typename T::V;
  T tr;
  ordered_map(long n) : tr(T(n)) {}
  std::optional<V> find(const K& k) {return tr.find_single(k);}
  std::optional<V> find_locked(const K& k) {return tr.find(k);}
  std::optional<V> find_(const K& k) {return tr.find_(k);}
  bool insert(const K& k , const V& v) {return tr.insert(k,v);}
  bool remove(const K& k) {return tr.remove(k);}
  long check() {return tr.size();}
  void print() {}
  static void clear() {}
  static void stats() {}
  static void shuffle(long n) {}
};
