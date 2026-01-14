#ifndef VERLIB_SKIPLIST_H_
#define VERLIB_SKIPLIST_H_

#include <verlib/verlib.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>

namespace verlib {
  
template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct skiplist {
  using K = K_;
  using V = V_;
  static constexpr auto less = Compare{};

  static constexpr int max_levels = 24;
  static constexpr int log_sample_factor = 1;
  static constexpr int num_bits = max_levels * log_sample_factor;

  // the level of a key is based on its hash
  int get_level(const K& key) {
    size_t mask = (1 << (num_bits - 1)) - 1;
    int keep_mask = (1 << log_sample_factor) - 1;
    size_t h = parlay::hash<K>{}(key) & mask;
    int level = 0;
    size_t hh = h;
    while ((h & keep_mask) == keep_mask) {
      level++;
      h = h >> log_sample_factor;
    }
    return level;
  }

  struct Val : verlib::versioned {
    V v;
    Val(V v) : v(v) {}
  };
  static verlib::memory_pool<Val> val_pool;
  
  template <int NumLevels>
  struct alignas(32) Node : verlib::versioned, verlib::lock {
    K key;
    verlib::versioned_ptr<Val> value;
    int level;
    verlib::atomic_bool removed;
    verlib::versioned_ptr<Node<0>> next[NumLevels+1];
    Node(const K& k, const V& v, int level)
      : key(k), value(val_pool.New(v)), level(level), removed(false) {}
    // for the root
    Node() : value(nullptr), level(max_levels-1), removed(false) {
      for (int i=0; i < max_levels; i++)
        next[i] = nullptr;
    }
  };

  using node = Node<0>;

  // Memory management
  // two types of node to save space
  static constexpr int small_cutoff = 4;
  using tallNode = Node<max_levels>;
  using shortNode = Node<small_cutoff>;
  static verlib::memory_pool<tallNode> tall_node_pool;
  static verlib::memory_pool<shortNode> short_node_pool;
  
  node* new_node(const K& k, const V& v, int level) {
    if (level > small_cutoff) return (node*) tall_node_pool.New(k, v, level);
    else return (node*) short_node_pool.New(k, v, level);
  }
  void retire_node(node* v) {
    Val* x = v->value.load();
    if (x != nullptr) val_pool.Retire(x);
    if (v->level > small_cutoff) tall_node_pool.Retire((tallNode*) v);
    else short_node_pool.Retire((shortNode*) v);
  }
  
  node* root;

  // find the node before and after the key (or at if in there) on the given level
  auto find_at_level(const K& k, int find_level) {
    node* prev = root;
    node* nxt = nullptr;
    int level = max_levels - 1;
    while (level >= find_level) {
      nxt = prev->next[level].load();
      if (nxt == nullptr || !less(nxt->key, k)) level--;
      else prev = nxt;
    }
    return std::make_pair(prev, nxt);
  }

  // Inserts a node at given level (and recursively below)
  // Works recursively by finding where to splice in and locking the previous node.
  // Splicing happens on way up the recursion.
  bool insert_at_level(node* cur, node* nxt, node* new_n, int level) {
    return cur->try_lock([=] {
      if (!verlib::validate([&] {return !cur->removed.load() && cur->next[level].load() == nxt;}))
        return false;
      if (level > 0) {
        node* dcur = cur;
        node* dnext = cur->next[level - 1].load();
        while (dnext != nullptr && !less(new_n->key, dnext->key)) {
          dcur = dnext;
          dnext = dcur->next[level - 1].load();
        }
        if (!insert_at_level(dcur, dnext, new_n, level - 1))
          return false;
      }
      new_n->next[level] = cur->next[level].load();
      cur->next[level] = new_n;
      return true;});}
  
  bool insert_(const K& k, const V& v) {
    return flck::try_loop([&] () -> std::optional<bool> {
        int level = get_level(k);
        auto [cur, nxt] = find_at_level(k, level);
        if (!(nxt == nullptr) && !less(k, nxt->key)) // already there
          if (cur->read_lock([&] {return verlib::validate([&] {
            return (cur->next[level].load() == nxt) && !cur->removed.load();});}))
            return false;
          else return {};
        node* new_n = new_node(k, v, level);
        if (insert_at_level(cur, nxt, new_n, level)) return true;
        return std::optional<bool>();});
  }
  
  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  bool upsert_(const K& k, const V& v) {
    return flck::try_loop([&] () -> std::optional<bool> {
        int level = get_level(k);
        auto [cur, nxt] = find_at_level(k, level);
        if (!(nxt == nullptr) && !less(k, nxt->key)) { // found
          if (nxt->try_lock([=] {
            if (!verlib::validate([&] {return !nxt->removed.load();}))
              return false;
            Val* oldv = nxt->value.load();
            nxt->value = val_pool.New(v); // update the value
            val_pool.Retire(oldv);
            return true;})) return false;
          else return {};
        }
        // not found
        node* new_n = new_node(k, v, level);
        if (insert_at_level(cur, nxt, new_n, level))
          return true;
        return std::optional<bool>();});
  }

  bool upsert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return upsert_(k, v);}); }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }

  // Recursively locks prev node of to_delete on each level going down
  // from level.  When reaching the bottom it goes through top-down
  // and splices out to_delete from each level.
  bool remove_at_level(node* start, int start_level, node* prev, node* to_delete, int level) {
    return prev->try_lock([=] {
      if (!verlib::validate([&] {return !prev->removed.load() && prev->next[level].load() == to_delete;}))
        return false;
      if (level > 0) {
        node* dprev = prev;
        node* nxt;
        while ((nxt = dprev->next[level - 1].load()) != to_delete)
          dprev = nxt;
        return remove_at_level(start, start_level, dprev, to_delete, level - 1);
      }
      // when we reach the bottom, all prev nodes are locked
      // now, starting at the start level, going down, we splice out to_delete
      return to_delete->try_lock([=] {
        node* dprev = start;
        //if (dprev->next[start_level].load() != to_delete) abort();
        dprev->next[start_level] = to_delete->next[start_level].load();
        for (int l = start_level-1; l >=0; l--) {
          node* nxt;
          while ((nxt = dprev->next[l].load()) != to_delete) // find where to splice out
            dprev = nxt;
          if (l == 0 && prev != dprev) abort();
          // splice out
          dprev->next[l] = to_delete->next[l].load();}
        to_delete->removed = true;
        return true;});});
  }

  bool remove_(const K& k) {
    return flck::try_loop([&] () -> std::optional<bool> {
        int level = get_level(k);
        auto [cur, nxt] = find_at_level(k, level);
        // if not found, then validate and return false
        if ((nxt == nullptr) || less(k, nxt->key)) 
          if (cur->read_lock([&] {return verlib::validate([&] {
            return (cur->next[level].load() == nxt) && !cur->removed.load();});}))
            return false;
          else return {};
        if (remove_at_level(cur, level, cur, nxt, level)) {
          retire_node(nxt); // succeeded
          return true;
        }
        else return std::optional<bool>();});
  }


  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<V> find_locked(const K& k) {
    using ot = std::optional<std::optional<V>>;
    return flck::try_loop([&] () -> ot {
      auto [cur, nxt] = find_at_level(k, 0);
      if (cur->read_lock([&] { return verlib::validate([&] {
      return (cur->next[0].load() == nxt) && !cur->removed.load();});})) {
        if (nxt != nullptr && !less(k, nxt->key))
          return std::optional(nxt->value.load()->v);
        else return std::optional(std::optional<V>());
        }
      else return ot();
      });
  }

  std::optional<V> find_(const K& k) {
    auto [cur, nxt] = find_at_level(k, 0);
    if  (nxt != nullptr && !less(k, nxt->key)) return nxt->value.load()->v;
    else return {};
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] {return find_(k);});
  }

  template<typename AddF>
  void range_(AddF& add, const K& start, const K& end) {
    auto [cur, nxt] = find_at_level(start, 0);
    while (nxt != nullptr && !less(end, nxt->key)) {
      add(nxt->key, nxt->value.load()->v);
      nxt = nxt->next[0].load();
    }
  }

  skiplist() : root((node*) tall_node_pool.New()) {}
  skiplist(long n) : root((node*) tall_node_pool.New()) {}

  void print() {
    node* ptr = (root->next[0]).load();
    while (ptr != nullptr) {
      std::cout << ptr->key << ", ";
      ptr = (ptr->next[0]).load();
    }
    std::cout << std::endl;
  }

  ~skiplist() {
    node* p = root;
    std::vector<node*> tops;
    while (p != nullptr) {
      tops.push_back(p);
      p = p->next[10].load();
    }
    parlay::parallel_for(0, tops.size(), [&] (long i) {
      node* p = tops[i];
      do {
        node* tmp = p;
        p = p->next[0].load();
        retire_node(tmp);
      } while (p != nullptr && p->level < 10);});
  }

  long size() { return check(); }
      
  long check() {
    node* p = root;
    std::vector<node*> tops;
    while (p != nullptr) {
      tops.push_back(p);
      p = p->next[10].load();
    }
    return parlay::reduce(parlay::tabulate(tops.size(), [&] (long i) {
      node* p = tops[i];
      node* nxt = p->next[0].load();
      long cnt = 0;
      while (nxt != nullptr && nxt->level < 10) {
	if (!(i == 0 && cnt == 0) && !less(p->key, nxt->key)) {
	  std::cout << "in skiplist: keys out of order";
	  abort();
	}
	p = nxt;
	nxt = p->next[0].load();
	cnt++;
      }
      if (!(i == 0 && cnt == 0) && nxt != nullptr && !less(p->key, nxt->key)) {
	std::cout << "in skiplist: keys out of order" << std::endl;
	abort();
      }
      return cnt+1;})) - 1;
  }

  static void clear() { tall_node_pool.clear(); short_node_pool.clear();}
  static void reserve(size_t n) { }
  static void shuffle(size_t n) { }
  static void stats() { tall_node_pool.stats(); short_node_pool.stats();}

};

template <typename K, typename V, typename C>
verlib::memory_pool<typename skiplist<K,V,C>::shortNode> skiplist<K,V,C>::short_node_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename skiplist<K,V,C>::tallNode> skiplist<K,V,C>::tall_node_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename skiplist<K,V,C>::Val> skiplist<K,V,C>::val_pool;

}
#endif //VERLIB_SKIPLIST_H_
