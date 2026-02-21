// MIT license (https://opensource.org/license/mit/)
// Initial Author: Guy Blelloch

// Nodes come in varying sizes and on update the node is copied.
#include <transactions.h>
#include <cmath>
#include <functional>
#include <iostream>
#include <optional>
#include <vector>
#include <parlay/parallel.h>
#include <parlay/primitives.h>

namespace parlay {
  
template <typename K_,
	  typename V_,
	  class Hash = std::hash<K_>,
	  class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;
private:
  enum State : char {Empty, Full};
  struct entry {State s; K k; V v;};
  std::vector<stm::atomic<entry>> entries;
  unsigned long mask;
  unsigned long table_size;
  //unsigned long hash(const K k) { return Hash{}(k) % table_size; }
  unsigned long hash(const K k) { return Hash{}(k) & mask; }
  unsigned long next(unsigned long i) {
    return (i + 1 == table_size) ? 0 : i + 1;}
  bool earlier(unsigned long l1, unsigned long l2) {
    //return ((l1 < l2 && (l2 - l1) < table_size/2) ||
    //        (l1 > l2 && (l1 - l2) > table_size/2));
    return ((l1 - l2) & mask) > table_size/2;
  }
  
public:

  std::optional<V> find(const K k) {
    long loc = hash(k);
    entry e = entries[loc].load();
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = entries[loc = next(loc)].load();
    if (KeyEqual{}(e.k, k)) return e.v;
    else return {};
  }

  bool insert(const K k, const V v) {
    long loc = hash(k);
    entry e = entries[loc].load();
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = entries[loc = next(loc)].load();
    if (KeyEqual{}(e.k, k)) return false;
    entries[loc].store(entry{Full,k,v});
    return true;
  }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    long loc = hash(k);
    entry e = entries[loc].load();
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = entries[loc = next(loc)].load();
    if (e.s = Full) {
      entries[loc].store(entry{Full,k,f(std::optional<V>(e.v))});
      return false;
    } else {
      entries[loc].store(entry{Full,k,f(std::optional<V>())});
      return true;
    }
  }

  bool upsert(const K& k, const V& v) {
    long loc = hash(k);
    entry e = entries[loc].load();
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = entries[loc = next(loc)].load();
    entries[loc].store(entry{Full,k,v});    
    if (e.s = Full) return false;
    else return true;
  }

  void backfill(unsigned long rloc) {
    unsigned long loc = next(rloc);
    entry e = entries[loc].load();
    while (e.s == Full && earlier(rloc, hash(e.k)))
      e = entries[loc = next(loc)].load();
    if (e.s == Full) {
      entries[rloc].store(e);
      backfill(loc);
    } else entries[rloc].store(entry{Empty,K{},V{}});
  }

  bool remove(const K k) {
    long loc = hash(k);
    entry e = entries[loc].load();
    while ((e.s == Full && !KeyEqual{}(e.k, k))) 
      e = entries[loc = next(loc)].load();
    if (!KeyEqual{}(e.k, k)) return false;
    backfill(loc);
    return true;
  }

  unordered_map(size_t n) {
    int bits = 1 + std::ceil(std::log2(1.5*n));
    table_size = 1ul << bits;
    mask = table_size - 1;
    entries = std::vector<stm::atomic<entry>>(table_size);
  }
    
  ~unordered_map() {}

  long size() {
    return parlay::count_if(entries, [&] (stm::atomic<entry>& e) {return (long) (e.load().s == Full);});
  }

  void print() {}
  long check() { return size();}
};
} //end namespace parlay
