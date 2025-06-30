#ifndef UHASHMAP_H_
#define UHASHMAP_H_

#include <vector>
#include <cmath>

template <typename K, typename V>
struct uhashMap {
  using KV = std::pair<K, V>;
  static constexpr K empty = (K) 0ul;
  int bits = 5;
  std::vector<KV> entries;
  size_t mask = 0;
  long num_entries = 0;
  size_t hash(K const& k) const noexcept {
    auto x =  (uint64_t)(k) * UINT64_C(0xbf58476d1ce4e5b9); 
    return (x ^ (x >> 31)); }  // linear then non-linear transform
  
  std::optional<V> find(const K k) {
    int loc = hash(k) & mask;
    if (entries.size() == 0) return {};
    while (entries[loc].first != empty && entries[loc].first != k)
      loc = (loc + 1) & mask;
    if (entries[loc].first == k) return entries[loc].second;
    else return {};
  }

  void insert_(const K k, const V v) {
    int loc = hash(k) & mask;
    while (entries[loc].first != empty && entries[loc].first != k)
      loc = (loc + 1) & mask;
    entries[loc] = KV(k,v);
    num_entries++;
  }
  
  void insert(const K k, const V v) {
    if (num_entries >= entries.size()/2) {
      bits = bits + 1;
      std::vector<KV> new_entries(1ul << bits, KV(empty,V{}));
      mask = new_entries.size() - 1;
      swap(entries, new_entries);
      for (auto kv : new_entries)
        if (kv.first != empty) insert_(kv.first,kv.second);
    }
    insert_(k,v);
  }
  
  void reset() {
    num_entries = 0;
    for (auto &a : entries) a.first = empty;
  }
  
  uhashMap() {}
};

#endif // UHASHMAP_H_
