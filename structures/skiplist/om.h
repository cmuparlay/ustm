#include <transactions.h>
#include <parlay/primitives.h>
#include <parlay/sequence.h>

#include <functional>
#include <iostream>
#include <optional>
#include <vector>

template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct ordered_map {
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

  template <int NumLevels>
  struct alignas(32) Node : stm::versioned {
    K key;
    V value;
    int level;
    stm::atomic_bool removed;
    stm::atomic<Node<0>*> next[NumLevels+1];
    Node(const K& k, const V& v, int level)
      : key(k), value(v), level(level), removed(false) {
      for (int i=0; i < level+1; i++)
        next[i] = nullptr;
    }
    // for the root
    Node() : level(max_levels-1), removed(false) {
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
  
  node* new_node(const K& k, const V& v, int level) {
    if (level > small_cutoff) return (node*) stm::New<tallNode>(k, v, level);
    else return (node*) stm::New<shortNode>(k, v, level);
  }
  void retire_node(node* v) {
    if (v->level > small_cutoff) stm::Delete((tallNode*) v);
    else stm::Delete((shortNode*) v);
  }
  
  node* new_root() { return (node*) stm::New<tallNode>(); }

  node* root;

  // find the node before and after the key (or at if in there) on the given level
  auto find_at_level(node* cur, const K& k, int level, int find_level) {
    node* prev = cur;
    node* nxt = nullptr;
    while (level >= find_level) {
      nxt = prev->next[level].load_unprotected();
      if (nxt == nullptr || !less(nxt->key, k)) level--;
      else prev = nxt;
    }
    prev->removed.validate();
    prev->next[find_level].validate();
    return std::make_pair(prev, nxt);
  }

  bool is_found(node* prev, node* next, const K& k) {
    return (next != nullptr) && !less(k, next->key);
  }

  void insert_at(node* x, node* new_node, int level) {
    if (level > 0) {
      node* cur = x;
      node* nxt = cur->next[level-1].load();
      while (nxt != nullptr && less(nxt->key, new_node->key)) {
        cur = nxt;
        nxt = cur->next[level-1].load();
      }
      insert_at(cur, new_node, level-1);
    }
    new_node->next[level] = x->next[level].load();
    x->next[level] = new_node;
  }

  bool insert(const K& k, const V& v) {
    int ilevel = get_level(k);
    auto [cur, nxt] = find_at_level(root, k, max_levels - 1, ilevel);
    if (is_found(cur, nxt, k)) return false;
    insert_at(cur, new_node(k, v, ilevel), ilevel);
    return true;
  }

  bool upsert(const K& k, const V& v) {
    // int level = get_level(k);
    // auto [cur, nxt] = find_at_level(k, level);
    // if (!(nxt == nullptr) && !less(k, nxt->key)) { // found
    //   Val* oldv = nxt->value.load();
    //   nxt->value = val_pool.New(v); // update the value
    //   val_pool.Retire(oldv);
    //   return false;
    // }
    // // not found
    // node* new_n = new_node(k, v, level);
    // //if (insert_at_level(cur, nxt, new_n, level))
    // //  return true;
    return true;
  }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }
  
  bool remove(const K& k) {
    int llevel = get_level(k);
    int level = get_level(k);
    bool s0 = ustm::getDescriptor()->toAbort;
    node* prev, *to_delete;
    std::tie(prev, to_delete) = find_at_level(root, k, max_levels - 1, level);
    node* phold = prev;
    bool s1 = ustm::getDescriptor()->toAbort;
    if (prev == nullptr) {std::cout << "null prev" << std::endl; std::abort();}
    if (!is_found(prev, to_delete, k)) return false;
    prev->next[level] = to_delete->next[level].load();
    //if (prev->next[0].load() == nullptr) {std::cout << "wow" << std::endl; std::abort();}
    while (--level >= 0) {
      int cnt = 0;
      node* tmp = prev->next[level].load();
      if (tmp == nullptr) {
        auto stm = ustm::getStmState();
        auto d = ustm::getDescriptor();
        std::cout << "ouch0: " << s0 << ", " << s1 << ", " << d->toAbort << ", " << stm->ts.getStamp() << ", " << stm->ts.getStamp() << ", " << phold->key << ", " << (prev->key >> 56) << std::endl;
        ustm::checking = true;
        for (int i = 0; i < llevel+1; i++) {
          std::cout << "level: " << i << " : ";
          node* cur = root;
          while (cur != nullptr) {
            std::cout << (cur->key >> 56) << ":" << get_level(cur->key) << ", ";
            cur = cur->next[i].load();
          }
          std::cout << std::endl;
          std::cout << stm->getLock(&root->next[i]).getStatus() << std::endl;
          std::cout << (((ustm::link<node*>*) stm->getLock(&root->next[i]).getVersionList())->value->key >> 56) << std::endl;
          if (root->next[i].val != nullptr)
            std::cout << ((root->next[i].val)->key >> 56) << std::endl;
        }
        std::abort();
      }
      while (tmp != to_delete) {// find where to splice out
        prev = tmp;
        tmp = prev->next[level].load();
        if (tmp == nullptr) {std::cout << "ouch1" << std::endl; std::abort();}
        cnt++;
      }
      prev->next[level] = to_delete->next[level].load();
    }
    to_delete->removed = true;
    retire_node(to_delete);
    return true;
  }

  std::optional<V> find(const K& k) {
    auto [cur, nxt] = find_at_level(root, k, max_levels - 1, 0);
    if  (nxt != nullptr && !less(k, nxt->key)) return nxt->value;
    else return {};
  }

  template<typename AddF>
  void range(AddF& add, const K& start, const K& end) {
    auto [cur, nxt] = find_at_level(root, start, max_levels - 1, 0);
    while (nxt != nullptr && !less(end, nxt->key)) {
      add(nxt->key, nxt->value);
      nxt = nxt->next[0].load();
    }
  }

  ordered_map() : root(new_root()) {}
  ordered_map(long n) : root(new_root()) {}

  void print() {
    node* ptr = (root->next[0]).load();
    while (ptr != nullptr) {
      std::cout << ptr->key << ", ";
      ptr = (ptr->next[0]).load();
    }
    std::cout << std::endl;
  }

  ~ordered_map() {
    node* p = root;
    std::vector<node*> tops;
    while (p != nullptr) {
      tops.push_back(p);
      p = p->next[10].load();
    }
    parlay::parallel_for(0, tops.size(), [&] (long i) {
      node* p = tops[i];
      node* nxt = p->next[0].load();
      while (nxt != nullptr && nxt->level < 10) {
        node* tmp = nxt;
        nxt = nxt->next[0].load();
        retire_node(tmp);
      } });
    for (auto p : tops) retire_node(p);
  }

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
	  std::cout << "in skiplist: keys out of order" << std::endl;
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
};
