#ifndef TPCC_GLUE_HPP
#define TPCC_GLUE_HPP

#include "set.h"

template <typename K>
struct StructString {
  static constexpr int num_ints = sizeof(K)/4;
  static int length(K key) { return sizeof(K);}
  static int get_byte(K key, int pos) {
    int* kptr = (int*) &key;
    int byte = 3 - (pos & 3);
    return (kptr[pos/4] >> (byte<<3)) & 255;
  }
};

template <typename Map_>
struct RMap_ {
  using Map = Map_;
  using V = typename Map::V;
  using K = typename Map::K;
  //using String = typename Map::String;
  
  Map m;
  RMap_(long n) : m(Map(n)) {}
  ~RMap_() {
    //std::cout << "destructing map: " << std::endl;
    //m.stats();
  }
  long size() { return m.check();}

  std::optional<V> get(const K& k, long tid) { return m.find(k); }

  bool insert(const K& key, const V& value, long tid) {
    return m.insert(key, value);
  }

  bool put(const K& key, const V& value, long tid) {
    return m.upsert(key, value);
  }

  template <typename F>
  bool upsert_f(const K& key, const F& f, long tid) {
    return m.upsert_f(key, f);
  }

  bool remove(const K& k) {
    return m.remove(k);
  }

  void clear() { m.clear();}
  void stats() { m.stats();}
};

template <typename K, typename V>
#if defined(HASH)
using OrderedMap = RMap_<parlay::unordered_map<K,V>>;
#elif defined(MIX)
using OrderedMap = RMap_<parlay::ordered_map<K,V,StructString<K>>>;
#elif defined(RADIX)
using OrderedMap = RMap_<parlay::ordered_map<K,V,StructString<K>>>;
#else
using OrderedMap = RMap_<parlay::ordered_map<K,V>>;
#endif

#ifdef Mix
#include "unordered_map.h"
#endif

template <typename K, typename V>
#if defined(HASH)
using UnorderedMap = RMap_<parlay::unordered_map<K,V>>;
#elif defined(Mix)
//using UnorderedMap = RMap_<parlay::ordered_map<K,V,StructString<K>>>;
using UnorderedMap = RMap_<parlay::unordered_map<K,V>>;
#elif defined(RADIX)
using UnorderedMap = RMap_<parlay::ordered_map<K,V,StructString<K>>>;
#else
using UnorderedMap = RMap_<parlay::ordered_map<K,V>>;
#endif



#endif // TPCC_GLUE_HPP
