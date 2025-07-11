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
  std::vector<entry> entries;
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
    entry e = stm::load(entries[loc]);
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = stm::load(entries[loc = next(loc)]);
    if (KeyEqual{}(e.k, k)) return e.v;
    else return {};
  }

  bool insert(const K k, const V v) {
    long loc = hash(k);
    entry e = stm::load(entries[loc]);
    while (e.s == Full && !KeyEqual{}(e.k, k))
      e = stm::load(entries[loc = next(loc)]);
    if (KeyEqual{}(e.k, k)) return false;
    stm::store(entries[loc], entry{Full,k,v});
    return true;
  }

  
  void backfill(unsigned long rloc) {
    unsigned long loc = next(rloc);
    entry e = stm::load(entries[loc]);
    while (e.s == Full && earlier(rloc, hash(e.k)))
      e = stm::load(entries[loc = next(loc)]);
    if (e.s == Full) {
      stm::store(entries[rloc], e);
      backfill(loc);
    } else stm::store(entries[rloc], entry{Empty,K{},V{}});
  }

  bool remove(const K k) {
    long loc = hash(k);
    entry e = stm::load(entries[loc]);
    while ((e.s == Full && !KeyEqual{}(e.k, k))) 
      e = stm::load(entries[loc = next(loc)]);
    if (!KeyEqual{}(e.k, k)) return false;
    backfill(loc);
    return true;
  }

  unordered_map(size_t n) {
    int bits = 1 + std::ceil(std::log2(1.5*n));
    table_size = 1ul << bits;
    mask = table_size - 1;
    entries = std::vector<entry>(table_size);
  }
    
  ~unordered_map() {}

  long size() {
    return parlay::count_if(entries, [&] (entry& e) {return (long) (e.s == Full);});
  }

  void print() {}
  long check() { return size();}
};
