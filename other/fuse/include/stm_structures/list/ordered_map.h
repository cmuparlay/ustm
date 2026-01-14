#include <verlib/verlib.h>

template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;
  static constexpr auto less = Compare{};

  struct alignas(32) node : verlib::versioned {
    K key;
    verlib::versioned_ptr<node> next;
    bool is_end = false;
    verlib::atomic_bool removed = false;
    V value;
    node(K key, V value, node* next) : key(key), value(value), next(next) {}
    node(node* next, bool is_end) : next(next), is_end(is_end) {};
  };

  node* root;
  
  static verlib::memory_pool<node> node_pool;

  static auto find_location(node* root, const K& k) {
    node* cur = root;
    node* nxt = (cur->next).load_weak();
    while (true) {
      if (nxt->is_end || !less(nxt->key, k)) break;
      cur = nxt;
      nxt = (nxt->next).load_weak(); // could prefetch
    }
    cur->removed.validate(false);
    cur->next.validate(nxt);
    //cur->removed.validate();
    //cur->next.validate();
    return std::make_pair(cur, nxt);
  }

  bool insert(const K& k, const V& v) {
    auto [cur, nxt] = find_location(root, k);
    if (!nxt->is_end && !less(k, nxt->key))
      return false;
    node* new_node = node_pool.New(k, v, nxt);
    cur->next = new_node; // splice in
    return true;
  }

  bool insert_(const K& k, const V& v) { return insert(k);} 

  bool remove(const K& k) {
    auto [cur, nxt] = find_location(root, k);
    if (nxt->is_end || less(k, nxt->key)) // not found
      return false;
    node* nxtnxt = (nxt->next).load();
    nxt->removed = true;
    cur->next = nxtnxt; // shortcut
    node_pool.Retire(nxt);
    return true;
  }

  bool remove_(const K& k) { return remove(k); }

  std::optional<V> find_(const K& k) {
    auto [cur, nxt] = find_location(root, k);
    if (!nxt->is_end && !less(k, nxt->key)) return nxt->value;
    else return {};
  }

  std::optional<V> find(const K& k) {
    return epoch::with_epoch([&] {return find_(k);}); }

  std::optional<V> find_locked(const K& k) { return find_(k);}

  template<typename AddF>
  void range_(AddF& add, const K& start, const K& end) {
    node* nxt = (root->next).load();
    while (true) {
      node* nxt_nxt = (nxt->next).load(); // prefetch
      if (nxt->is_end || !less(nxt->key, start)) break;
      nxt = nxt_nxt;
    }
    while (!nxt->is_end && !less(end, nxt->key)) {
      add(nxt->key, nxt->value);
      nxt = nxt->next.load();
    }
  }

  ordered_map() : root(node_pool.New(node_pool.New(nullptr,true),false)) {}
  ordered_map(size_t n) : root(node_pool.New(node_pool.New(nullptr,true),false)) {}

  void print() {
    node* ptr = (root->next).load();
    while (!ptr->is_end) {
      std::cout << ptr->key << ", ";
      ptr = (ptr->next).load();
    }
    std::cout << std::endl;
  }

  void retire_recursive(node* p) {
    if (!p->is_end) retire_recursive(p->next.load());
    node_pool.Retire(p);
  }

  ~ordered_map() {retire_recursive(root);}
  
  long check() {
    node* ptr = (root->next).load();
    if (ptr->is_end) return 0;
    K k = ptr->key;
    ptr = (ptr->next).load();
    long i = 1;
    while (!ptr->is_end) {
      i++;
      if (!less(k, ptr->key)) {
        std::cout << "bad key: " << k << ", " << ptr->key << std::endl;
        abort();
      }
      k = ptr->key;
      ptr = (ptr->next).load();
    }
    return i;
  }

  static void clear() { node_pool.clear();}
  static void reserve(size_t n) { node_pool.reserve(n);}
  static void shuffle(size_t n) { } // {node_pool.shuffle(n);}
  static void stats() { node_pool.stats();}

};

template <typename K, typename V, typename C>
verlib::memory_pool<typename ordered_map<K,V,C>::node> ordered_map<K,V,C>::node_pool;
