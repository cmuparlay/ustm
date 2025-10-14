#include <transactions.h>
#include <functional>
#include <iostream>
#include <optional>
#include <utility>

template <typename K_,
          typename V_,
	        class Hash = std::hash<K_>,
	        class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;

  //struct alignas(64) node : stm::versioned {
  struct node : stm::versioned {
    stm::atomic<K> key;
    stm::atomic<V> value;
    node(K key, V value) : key(key), value(value) {}
  };

  static constexpr size_t default_sz = 5000;
  size_t sz;
  node* arr;

  inline node& find_location(const K& k) {
    size_t idx = Hash{}(k) % sz;
    return arr[idx];
  }

  bool insert(const K& k, const V& v) {
    node& n = find_location(k);
    if (!KeyEqual{}(n.key.load(), k)) {
      n.value.store(v);
      return true;
    }
    return false; //Key already there
  }

  bool remove(const K& k) {
    node& n = find_location(k);
    n.value.store(V{});
    return true;
  }

  std::optional<V> find(const K& k) {
    node& n = find_location(k);
    if (KeyEqual{}(n.key.load(), k)) return n.value.load();
    else return {};
  }

  unordered_map(size_t n) : sz(n) {
    arr = (node*) malloc(sizeof(node) * sz);
    for(int i = 0; i < sz; i++) { //Initialize all nodes with default values
      node& n = arr[i];
      n.key.store(K{});
      n.value.store(V{});
    }
  }
  unordered_map() : unordered_map(default_sz) {}
  ~unordered_map() { free(arr); }

  long check() { return 0;}
};
