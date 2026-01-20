// MIT license (https://opensource.org/license/mit/)
// Initial Author: Guy Blelloch

// Nodes come in varying sizes and on update the node is copied.
#include <optional>
#include <parlay/primitives.h>
#include <verlib/verlib.h>

template <typename K_,
	  typename V_,
	  class Hash = std::hash<K_>,
	  class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;
private:
  struct KV {K key; V value;};

  template <typename Range>
  static int find_in_range(const Range& entries, long cnt, const K& k) {
    for (int i=0; i < cnt; i++)
      if (KeyEqual{}(entries[i].key, k)) return i;
    return -1;
  }

  // The following three functions copy a range and
  // insert/update/remove the specified key.  No ordering is assumed
  // within the range.  Insert assumes k does not appear, while
  // update/remove assume it does appear.
  template <typename Range, typename RangeIn>
  static void copy_and_insert(Range& out, const RangeIn& entries, long cnt, const K& k, const V& v) {
    for (int i=0; i < cnt; i++) out[i] = entries[i];
    out[cnt] = KV{k,v};
  }

  template <typename Range, typename RangeIn, typename F>
  static void copy_and_update(Range& out, const RangeIn& entries, long cnt, const K& k, const F& f) {
    int i = 0;
    while (!KeyEqual{}(k, entries[i].key) && i < cnt) {
      assert(i < cnt);
      out[i] = entries[i];
      i++;
    }
    out[i].key = entries[i].key;
    out[i].value = f(std::optional(entries[i].value));
    i++;
    while (i < cnt) {
      out[i] = entries[i];
      i++;
    }
  }

  template <typename Range, typename RangeIn>
  static void copy_and_remove(Range& out, const RangeIn& entries, long cnt, const K& k) {
    int i = 0;
    while (!KeyEqual{}(k, entries[i].key)) {
      assert(i < cnt);
      out[i] = entries[i];
      i++;
    }
    while (i < cnt-1) {
      out[i] = entries[i+1];
      i++;
    }
  }

  // Each bucket points to a Node of some Size, or to a BigNode (defined below)
  // A node contains an array of up to Size entries (actual # of entries given by cnt)
  // Sizes are 1, 3, 7, 31
  template <int Size>
  struct alignas(32) Node : verlib::versioned {
    using node = Node<0>;
    int cnt;
    KV entries[Size];

    // return index of key in entries, or -1 if not found
    int find_index(const K& k) {
      if (cnt <= 31) return find_in_range(entries, cnt, k);
      else return find_in_range(((BigNode*) this)->entries, cnt, k);
    }

    // return optional value found in entries given a key
    std::optional<V> find(const K& k) {
      if (cnt <= 31) { // regular node
	if (KeyEqual{}(entries[0].key, k)) // shortcut for common case
          return entries[0].value;
	int i = find_in_range(entries+1, cnt-1, k);
	if (i == -1) return {};
	else return entries[i].value;
      } else { // big node
	int i = find_in_range(((BigNode*) this)->entries, cnt, k);
	if (i == -1) return {};
	else return ((BigNode*) this)->entries[i].value;
      }
    }

    // copy and insert
    Node(node* old, const K& k, const V& v) {
      cnt = (old == nullptr) ? 1 : old->cnt + 1;
      copy_and_insert(entries, old->entries, cnt-1, k, v);
    }

    // copy and update
    template <typename F>
    Node(node* old, const K& k, const F& f) : cnt(old->cnt) {
      assert(old != nullptr);
      copy_and_update(entries, old->entries, cnt, k, f);
    }

    // copy and remove
    Node(node* old, const K& k) : cnt(old->cnt - 1) {
      if (cnt == 31) copy_and_remove(entries, ((BigNode*) old)->entries, cnt+1, k);
      else copy_and_remove(entries, old->entries, cnt+1, k);
    }

    // singleton
    Node(const K& k, const V& v) : cnt(1) {
      entries[0].key = k;
      entries[0].value = v;
    }
  };
  using node = Node<0>;

  // If a node overflows (cnt > 31), then it becomes a big node and its content
  // is stored indirectly in a parlay sequence.
  struct BigNode : verlib::versioned {
    using entries_type = std::vector<KV>;
    int cnt;
    entries_type entries;

    // copy and insert
    BigNode(node* old, const K& k, const V& v) : cnt(old->cnt + 1) {
      entries = entries_type(cnt);
      if (old->cnt == 31) copy_and_insert(entries, old->entries, old->cnt, k, v);
      else copy_and_insert(entries, ((BigNode*) old)->entries, old->cnt, k, v);
    }

    // copy and update
    template <typename F>
    BigNode(node* old, const K& k, const F& f) : cnt(old->cnt) {
      entries = entries_type(cnt);
      copy_and_update(entries, ((BigNode*) old)->entries, cnt, k, f);  }

    // copy and remove
    BigNode(node* old, const K& k) : cnt(old->cnt - 1) {
      entries = entries_type(cnt);
      copy_and_remove(entries, ((BigNode*) old)->entries, cnt+1, k); }
  };

  // structure for each bucket
  struct bucket {
    node* load() {return ptr.load();}
    verlib::versioned_ptr<node> ptr;
    bucket() : ptr(nullptr) {}
  };

  struct Table {
    std::vector<bucket> table;
    size_t size;
    bucket* get_bucket(const K& k) {
      size_t idx = Hash{}(k)  & (size-1u);
      return &table[idx];
    }
    Table(size_t n) {
      int bits = 1 + parlay::log2_up(n);
      size = 1ul << bits;
      table = std::vector<bucket>(size);
    }
  };

  Table hash_table;

  using Node1 = Node<1>;
  using Node3 = Node<3>;
  using Node7 = Node<7>;
  using Node31 = Node<31>;
  static verlib::memory_pool<Node1> node_pool_1;
  static verlib::memory_pool<Node3> node_pool_3;
  static verlib::memory_pool<Node7> node_pool_7;
  static verlib::memory_pool<Node31> node_pool_31;
  static verlib::memory_pool<BigNode> big_node_pool;

  static node* insert_to_node(node* old, const K& k, const V& v) {
    if (old == nullptr) return (node*) node_pool_1.New(old, k, v);
    if (old->cnt < 3) return (node*) node_pool_3.New(old, k, v);
    else if (old->cnt < 7) return (node*) node_pool_7.New(old, k, v);
    else if (old->cnt < 31) return (node*) node_pool_31.New(old, k, v);
    else return (node*) big_node_pool.New(old, k, v);
  }

  template <typename F>
  static node* update_node(node* old, const K& k, const F& f) {
    if (old->cnt == 1) return (node*) node_pool_1.New(old, k, f);
    if (old->cnt <= 3) return (node*) node_pool_3.New(old, k, f);
    else if (old->cnt <= 7) return (node*) node_pool_7.New(old, k, f);
    else if (old->cnt <= 31) return (node*) node_pool_31.New(old, k, f);
    else return (node*) big_node_pool.New(old, k, f);
  }

  static node* remove_from_node(node* old, const K& k) {
    if (old->cnt == 1) return (node*) nullptr;
    if (old->cnt == 2) return (node*) node_pool_1.New(old, k);
    else if (old->cnt <= 4) return (node*) node_pool_3.New(old, k);
    else if (old->cnt <= 8) return (node*) node_pool_7.New(old, k);
    else if (old->cnt <= 32) return (node*) node_pool_31.New(old, k);
    else return (node*) big_node_pool.New(old, k);
  }

  static void retire_node(node* old) {
    if (old == nullptr);
    else if (old->cnt == 1) node_pool_1.Retire((Node1*) old);
    else if (old->cnt <= 3) node_pool_3.Retire((Node3*) old);
    else if (old->cnt <= 7) node_pool_7.Retire((Node7*) old);
    else if (old->cnt <= 31) node_pool_31.Retire((Node31*) old);
    else big_node_pool.Retire((BigNode*) old);
  }

public:
  unordered_map(size_t n) : hash_table(Table(n)) {}
  ~unordered_map() {
    auto& table = hash_table.table;
    parlay::parallel_for (0, table.size(), [&] (size_t i) {
      retire_node(table[i].load());});
  }
  
  std::optional<V> find(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    __builtin_prefetch (s);
    return verlib::with_epoch([&] () -> std::optional<V> {
      auto x = s->load();
      if (x == nullptr) return {};
      return x->find(k);
    });
  }

  std::optional<V> find_(const K& k) {
    auto x = hash_table.get_bucket(k)->load();
    if (x == nullptr) return std::nullopt;
    return x->find(k);
  }

  std::optional<V> find_locked(const K& k) {
    return find(k);} 
      
  bool insert(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    node* old_node = s->load();
    if (old_node != nullptr && old_node->find_index(k) != -1)
      return false;
    s->ptr = insert_to_node(old_node, k, v);
    retire_node(old_node);
    return true;
  }

  bool insert_(const K& k, const V& v) { return insert_(k, v); }

  bool upsert(const K& k, const V& v) {
    bucket* s = hash_table.get_bucket(k);
    node* old_node = s->load();
    if (old_node != nullptr && old_node->find_index(k) != -1) {
      auto f = [&] (auto x) {return v;};
      s->ptr = update_node(old_node, k, f);      
      retire_node(old_node);
      return false;
    } else {
      s->ptr = insert_to_node(old_node, k, v);
      retire_node(old_node);
      return true;
    }
  }

  bool upsert_(const K& k, const V& v) { return upsert(k, v); }

  bool remove(const K& k) {
    bucket* s = hash_table.get_bucket(k);
    node* old_node = s->load();
    if (old_node == nullptr || old_node->find_index(k) == -1)
      return false;
    s->ptr = remove_from_node(old_node, k);
    retire_node(old_node);
    return true;
  }

  bool remove_(const K& k) { return remove(k);}

  long size() {
    auto& table = hash_table.table;
    auto s = parlay::tabulate(table.size(), [&] (size_t i) {
      node* x = table[i].load();
      if (x == nullptr) return 0;
      else return x->cnt;});
    return parlay::reduce(s);
  }

  void print() {
    auto& table = hash_table.table;
    for (size_t i=0; i < table.size(); i++) {
      node* x = table[i].ptr.load();
      if (x != nullptr)
	for (int i = 0; i < x->cnt; i++) {
	  K key = (x->cnt < 32) ? x->entries[i].key : ((BigNode*) x)->entries[i].key;
	  std::cout << key << ", ";
	}
    }
    std::cout << std::endl;
  }

  long check() { return size();}

  static void clear() {
    node_pool_1.clear();
    node_pool_3.clear();
    node_pool_7.clear();
    node_pool_31.clear();
    big_node_pool.clear();
  }
  static void stats() {
    node_pool_1.stats();
    node_pool_3.stats();
    node_pool_7.stats();
    node_pool_31.stats();
    big_node_pool.stats();
  }
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}

};

template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node1> unordered_map<K,V,H,E>::node_pool_1;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node3> unordered_map<K,V,H,E>::node_pool_3;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node7> unordered_map<K,V,H,E>::node_pool_7;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::Node31> unordered_map<K,V,H,E>::node_pool_31;
template <typename K, typename V, typename H, typename E>
verlib::memory_pool<typename unordered_map<K,V,H,E>::BigNode> unordered_map<K,V,H,E>::big_node_pool;

