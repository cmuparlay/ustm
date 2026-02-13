// MIT license (https://opensource.org/license/mit/)
// Initial Author: Guy Blelloch
// Developed as part of the flock library
// 

#ifndef PARLAYHASH_H_
#define PARLAYHASH_H_

#include <optional>
#include <transactions.h>
#include <parlay/primitives.h>

namespace parlay {

template <typename K_,
	  typename V_,
	  class Hash = std::hash<K_>,
	  class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;
  using KV = std::pair<K,V>;
  struct link :  stm::versioned {
    KV element;
    stm::atomic<link*> next;
    link(KV element, link* next) : element(element), next(next) {}
  };

  using lnk = stm::atomic<link*>;

  struct Table {
    parlay::sequence<lnk> table;
    size_t size;
    lnk* get_bucket(const K& k) {
      size_t idx = Hash{}(k) & (size-1u);
      return &table[idx];
    }
    Table(size_t n) {
      int bits = 1 + parlay::log2_up(n);
      size = 1ul << bits;
      table = parlay::sequence<lnk>(size);
    }
  };

  Table hash_table;

  unordered_map(size_t n) : hash_table(Table(n)) {}
    
  ~unordered_map() {
    auto& table = hash_table.table;
    parlay::parallel_for (0, table.size(), [&] (size_t i) {
      delete_list(table[i].load());});
  }

  // returns found link (or nullptr if not found)
  // and address of the pointer to that link
  static std::pair<link*, lnk*>
  find_in_list(lnk* ptr, const K& k) {
    link* nxt = ptr->load();
    while (nxt != nullptr && !KeyEqual{}(nxt->element.first, k)) {
      ptr = &(nxt->next);
      nxt = ptr->load();
    }
    return std::pair(nxt, ptr);
  }

  void delete_list(link* nxt) {
    while (nxt != nullptr) {
      link* tmp = nxt;
      nxt = nxt->next.load();
      stm::Delete(tmp);
    }
  }

  static int list_length(link* nxt) {
    int len = 0;
    while (nxt != nullptr) {
      len++;
      nxt = nxt->next.load();
    }
    return len;
  }
    
  std::optional<V> find(const K& k) {
    lnk* s = hash_table.get_bucket(k);
    auto [lnk, ptr] = find_in_list(s, k);
    if (lnk == nullptr) return {};
    else return lnk->element.second;
  }

  bool insert(const K& k, const V& v) {
    lnk* s = hash_table.get_bucket(k);
    auto [lnk, ptr] = find_in_list(s, k);
    if (lnk == nullptr) {
      ptr->store(stm::New<link>(KV{k,v}, nullptr));
      return true;
    } return false;
  }

  bool remove(const K& k) {
    lnk* s = hash_table.get_bucket(k);
    auto [lnk, ptr] = find_in_list(s, k);
    if (lnk == nullptr) return false;
    ptr->store(lnk->next.load());
    stm::Delete(lnk);
    return true;
  }

  long size() {
    return parlay::reduce(parlay::map(hash_table.table, [] (lnk& x) {
                             return (long) list_length(x.load());}));
  }

  void print() {}
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}
  static void stats() {}
  long check() { return size();}
  static void clear() {}

};

}  // end namespace parlay

#endif // PARLAYHASH_H_
