#ifndef VERLIB_BTREE_H_
#define VERLIB_BTREE_H_

#include <verlib/verlib.h>
#include <parlay/primitives.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <tuple>
#include <type_traits>

// A top-down implementation of abtrees
// Nodes are split or joined on the way down to ensure that each node
// can fit one more child, or remove one more child.

namespace verlib {
  
template <typename K_,
          typename V_,
          typename Compare = std::less<K_>>
struct btree {
  using K = K_;
  using V = V_;
  static verlib::memory_pool<V> value_pool;

  struct VP_indirect {
    V* vp;
    V get_value() { return *vp; }
    void retire() const { value_pool.Retire(vp); }
    VP_indirect() {}
    VP_indirect(const V& v) : vp(value_pool.New(v)) {}
  };

  struct VP_direct {
    V v;
    V get_value() { return v; }
    void retire() const {}
    VP_direct() {}
    VP_direct(const V& v) : v(v) {}
  };

  static constexpr bool use_indirect = (sizeof(V) + sizeof(K)) >= 64;
  using VP = std::conditional_t<use_indirect, VP_indirect, VP_direct>;

  static constexpr auto less = Compare{};
  struct KV {K key; VP value;};

  struct leaf;
  struct node;
  node* root;
  enum Status : char { isOver, isUnder, OK};

  struct header : verlib::versioned {
    bool is_leaf;
    Status status;
    verlib::atomic_bool removed; 
    int size;
    header(bool is_leaf, Status status, int size)
      : is_leaf(is_leaf), status(status), removed(false), size(size) {}
  };

  static constexpr int leaf_block_bytes = 64 * 8;
  static constexpr int kv_bytes = leaf_block_bytes - sizeof(header);
  static constexpr int leaf_block_size = std::max(kv_bytes/sizeof(KV), 12ul);
  static constexpr int leaf_min_size = leaf_block_size/5;
  static constexpr int leaf_join_cutoff = leaf_min_size * 4;

  static constexpr int node_block_bytes = 64 * 6;
  static constexpr int entry_bytes = node_block_bytes - sizeof(header) - sizeof(verlib::lock) + sizeof(K);
  static constexpr int node_block_size = std::max(entry_bytes/(sizeof(K) + 8), 5ul);
  static constexpr int node_min_size = node_block_size/5;
  static constexpr int node_join_cutoff = node_min_size * 4;

  // Internal Node
  // Has node_block_size children, and one fewer keys to divide them
  // The only mutable fields are the child pointers (children) and the
  // removed flag, which is only written to once when the node is
  // being removed.
  // A node needs to be copied to add, remove, or rebalance children.
  struct alignas(64) node : header {
    K keys[node_block_size-1];
    verlib::versioned_ptr<node> children[node_block_size];
    verlib::lock lck;
    
    int find(const K& k, int i=0) {
      //while (i < header::size-1 && !less(k, keys[i])) i++;
      //return i;
      if (header::size == 1) return 0;
      int mid = (header::size-1)/2;
      if (less(k, keys[mid])) // first half
        while (!less(k, keys[i])) i++;
      else {
        i = std::max(i, mid+1);
        while (i < header::size-1 && !less(k, keys[i])) i++;
      }
      return i;
    }

    Status check_size(long n) {
      if (n == node_min_size) return isUnder;
      else if (n == node_block_size) return isOver;
      else return OK;
    }

    // create with given size, to be filled in
    node(int size) : header{false, check_size(size), size} {}

    // create with a single child (just the pointer to the root)
    node(leaf* l) : header{false, OK, 1} {
      children[0].init((node*) l);
    }

    // create with two children separated by a key (a new root node)
    node(std::tuple<node*,K,node*> nec) : header{false, OK, 2} {
      auto [left,k,right] = nec;
      keys[0] = k;
      children[0].init(left);
      children[1].init(right);
    }

    // copy a node using access functions
    template <typename Key, typename Child>
    node(int size, Key get_key, Child get_child)
      : header{false, check_size(size), size} {
      for (int i = 0; i < size; i++) children[i].init(get_child(i));
      for (int i = 0; i < size-1; i++) keys[i] = get_key(i);
    }

  };

  static verlib::memory_pool<node> node_pool;


    
  // helper function for split and rebalance
  template <typename Key, typename Child>
  static std::tuple<node*,K,node*> split_mid(int size, Key get_key, Child get_child) {
    // copy into new left child
    int lsize = size/2;
    node* new_l = node_pool.New(lsize, get_key, get_child);

    // copy into new right child
    node* new_r = node_pool.New(size - lsize,
                                [&] (int i) {return get_key(i + lsize);},
                                [&] (int i) {return get_child(i + lsize);});

    // the dividing key
    K mid = get_key(lsize-1);
    return std::make_tuple(new_l, mid, new_r);
  }

  // split an internal node into two nodes returning the new nodes
  // and the key that divides the children
  static std::tuple<node*,K,node*> split(node* p) {
    assert(p->size == node_block_size);
    auto result = split_mid(p->size,
                            [=] (int i) {return p->keys[i];},
                            [=] (int i) {return p->children[i].load();});
    return result;
  }

  // rebalances two nodes into two new nodes
  static std::tuple<node*,K,node*> rebalance(node* c1, const K& k, node* c2) {
    auto get_key = [=] (int i) {
      if (i < c1->size-1) return c1->keys[i];
      else if (i == c1->size-1) return k;
      else return c2->keys[i-c1->size];};
    auto get_child = [=] (int i) {
      if (i < c1->size) return c1->children[i].load();
      else return c2->children[i-c1->size].load();};
    auto result = split_mid(c1->size + c2->size, get_key, get_child);
    return result;
  }

  // joins two nodes into one
  static node* join(node* c1, const K& k, node* c2) {
    int size = c1->size + c2->size;
    auto get_key = [=] (int i) {
      if (i < c1->size-1) return c1->keys[i];
      else if (i == c1->size-1) return k;
      else return c2->keys[i-c1->size];};
    auto get_child = [=] (int i) {
      if (i < c1->size) return c1->children[i].load();
      else return c2->children[i-c1->size].load();};
    node* new_p = node_pool.New(size, get_key, get_child); 
    return new_p;
  }

  // Update an internal node given a child that is split into two
  static node* add_child(node* p, std::tuple<node*,K,node*> newc, int pos) {
    int size = p->size;
    assert(size < node_block_size);
    auto [c1,k,c2] = newc;
    auto get_key = [=] (int i) {
      if (i < pos) return p->keys[i];
      else if (i == pos) return k;
      else return p->keys[i-1];};
    auto get_child = [=] (int i) {
      if (i < pos) return p->children[i].load();
      else if (i == pos) return c1;
      else if (i == pos+1) return c2;
      else return p->children[i-1].load();};
    node* new_p = node_pool.New(size+1, get_key, get_child);
    return new_p;
  }

  // Update an internal node replacing two adjacent children
  // with a new child
  static node* join_children(node* p, node* c, int pos) {
    int size = p->size;
    auto get_key = [=] (int i) {
      if (i < pos) return p->keys[i];
      else return p->keys[i+1];};
    auto get_child = [=] (int i) {
      if (i < pos) return p->children[i].load();
      else if (i == pos) return c;
      else return p->children[i+1].load();};
    node* new_p = node_pool.New(size-1, get_key, get_child);
    return new_p;
  }

  // Update an internal node replacing two adjacent children
  // with two newly rebalanced children
  static node* rebalance_children(node* p, std::tuple<node*,K,node*> newc, int pos) {
    int size = p->size;
    auto [c1,k,c2] = newc;
    auto get_key = [=] (int i) {
      if (i == pos) return k;
      else return p->keys[i];};
    auto get_child = [=] (int i) {
      if (i == pos) return c1;
      else if (i == pos+1) return c2;
      else return p->children[i].load();};
    node* new_p = node_pool.New(size, get_key, get_child);
    return new_p;
  }

  // ***************************************
  // Leafs
  // ***************************************

  // Leafs are immutable.  Once created and the keyvals set, their values
  // will not be changed.

  struct alignas(64) leaf : header {
    KV keyvals[leaf_block_size];
    
    int find(const K& k) {
      int i=0;
      // if (header::size == 0) return std::optional<V>();
      // int mid = header::size/2;
      // if (!less(keyvals[mid].key, k)) // first half
      //     while (less(keyvals[i].key, k)) i++;
      // else {
      //     i = mid+1;
      //     while (i < header::size && less(keyvals[i].key, k)) i++;
      // }
      while (i < header::size && less(keyvals[i].key, k)) i++;
      if (i == header::size || less(k, keyvals[i].key)) return -1;
      else return i;
    }

    std::optional<V> find_value(const K& k) {
      int i = find(k);
      if (i != -1) return keyvals[i].value.get_value();
      return {};
    }

    int prev(const K& k, int i=0) {
      while (i < header::size && less(keyvals[i].key, k)) i++;
      return i;
    }

    Status check_size(long n) {
      if (n == leaf_min_size) return isUnder;
      else if (n == leaf_block_size) return isOver;
      else return OK;
    }

    leaf(int size) : header{true, check_size(size), size} {};

    // join two leaves
    leaf(leaf* l, leaf* r) :
      header{true, check_size(l->size + r->size), (l->size + r->size)} {
      for (int i=0; i < l->size + r->size; i++)
        keyvals[i] = ((i < l->size) 
                      ? ((leaf*) l)->keyvals[i] 
                      : ((leaf*) r)->keyvals[i - l->size]);
    }

    // copy from sequence
    template <typename KeyVal>
    leaf(int size, int offset, KeyVal get_kv) : header{true, check_size(size), size} {
      for (int i = 0; i < size; i++) {
        auto [k,v] = get_kv(i+offset);
        keyvals[i].key = k; 
        keyvals[i].value = v;
      }
    }

    // remove a key
    leaf(leaf* l, const K& k) : header{true, check_size(l->size - 1), l->size - 1} {
      int size = l->size;
      assert(size > 0);
      int i=0;
      // part before the key
      for (;i < size && less(l->keyvals[i].key, k); i++) keyvals[i] = l->keyvals[i];
      // part after the key, shifted left
      for (; i < size-1 ; i++ ) keyvals[i] = l->keyvals[i+1];
    }

    // insert or upsert into a leaf 
    leaf(leaf* l, const K& k, const V& v, bool upsert)
      : header{true, check_size(upsert ? l->size : l->size + 1), (upsert ? l->size : l->size + 1)} {
      int size = l->size;
      assert(size < leaf_block_size);
      int i=0;

      // copy part before the new key
      for (;i < size && less(l->keyvals[i].key, k); i++)
        keyvals[i] = l->keyvals[i];

      // copy in the new key and value
      keyvals[i] = KV{k, VP(v)};
      if (upsert) {
	// retire old value
	l->keyvals[i].value.retire();
	// copy the part after the new key, skipping the key that is equal
	// can only happen when upserting
	i++;
        for (; i < size ; i++ ) keyvals[i] = l->keyvals[i];
      } else { // copy the part after the new key
	for (; i < size ; i++ ) keyvals[i+1] = l->keyvals[i];
      }
    }

  };
  
  static verlib::memory_pool<leaf> leaf_pool;

  // Insert a new key-value pair by copying into a new leaf.
  static leaf* insert_leaf(leaf* l, const K& k, const V& v, bool upsert=false) {
    return leaf_pool.New(l, k, v, upsert);
  }

  // Remove a key-value pair from the leaf that matches the key k.
  // This copies the values into a new leaf.
  static leaf* remove_leaf(leaf* l, const K& k) { return leaf_pool.New(l, k); }

  // helper function for split_leaf and rebalance_leaf
  template <typename KeyVal>
    static std::tuple<node*,K,node*> split_mid_leaf(int size, KeyVal get_kv) {
    int lsize = size/2;
    leaf* new_l = leaf_pool.New(lsize, 0, get_kv);
    leaf* new_r = leaf_pool.New(size - lsize, lsize, get_kv);
    return std::make_tuple((node*) new_l, get_kv(lsize).key, (node*) new_r);
  } 

  // split a leaf into two leaves returning the new nodes
  // and the first key of the right leaf
  static std::tuple<node*,K,node*> split_leaf(node* p) {
    leaf* l = (leaf*) p;
    int size = l->size;
    assert(size == leaf_block_size);
    auto result = split_mid_leaf(size, [=] (int i) {return l->keyvals[i];});
    return result;
  }

  static std::tuple<node*,K,node*> rebalance_leaf(node* l, node* r) {
    int size = l->size + r->size;
    auto result = split_mid_leaf(size, [=] (int i) {
      if (i < l->size) return ((leaf*) l)->keyvals[i];
      else return ((leaf*) r)->keyvals[i - l->size];});
    return result;
  }

  static node* join_leaves(node* l, node* r) {
    return (node*) leaf_pool.New((leaf*) l, (leaf*) r);
  }

  // ***************************************
  // Tree code
  // ***************************************

  // Splits an overfull node (i.e. one with block_size entries)
  // for a grandparent gp, parent p, and child c:
  // copies the parent p to replace with new children and
  // updates the grandparent gp to point to the new copied parent.
  static void overfull_node(node* gp, int pidx, node* p, int cidx, node* c) {
    gp->lck.try_lock([=] {
      // check that gp has not been removed, and p has not changed
      if (gp->removed.load() || gp->children[pidx].load() != p) return false;
      return p->lck.try_lock([=] {
        // check that c has not changed
        if (!verlib::validate([&] {return p->children[cidx].load() == c;}))
          return false;
        if (c->is_leaf) {
          gp->children[pidx] = add_child(p, split_leaf(c), cidx);
          p->removed = true;
          leaf_pool.Retire((leaf*) c);
          node_pool.Retire(p);
          return true;
        }
        return c->lck.try_lock([=] {
          gp->children[pidx] = add_child(p, split(c), cidx);
          p->removed = c->removed = true;
          node_pool.Retire(c);
          node_pool.Retire(p);
          return true;});
      });});
  }

  // Joins or rebalances an underfull node (i.e. one with min_size)
  // Picks one of the two neighbors and either joins with it if the sum of
  // the sizes is less than join_cutoff, or rebalances the two otherwise.
  // Copies the parent p to replace with new child or children.
  // Updates the grandparent gp to point to the new copied parent.
  static void underfull_node(node* gp, int pidx, node* p, int cidx, node* c) {
    gp->lck.try_lock([=] {
      // check that gp has not been removed, and p has not changed
      if (!verlib::validate([&] {return !gp->removed.load() && gp->children[pidx].load() == p;}))
        return false;
      return p->lck.try_lock([=] {
        // join with next if first in block, otherwise with previous
        node* other_c = p->children[(cidx == 0 ? cidx + 1 : cidx - 1)].load();
        auto [li, lc, rc] = ((cidx == 0) ?
                             std::make_tuple(0, c, other_c) :  
                             std::make_tuple(cidx-1, other_c, c)); 
        if (c->is_leaf) { // leaf
          if (lc->size + rc->size < leaf_join_cutoff)  // join
            gp->children[pidx] = join_children(p, join_leaves(lc, rc), li);
          else   // rebalance
            gp->children[pidx] = rebalance_children(p, rebalance_leaf(lc, rc), li);
          p->removed = true;
          node_pool.Retire(p);
          leaf_pool.Retire((leaf*) lc);
          leaf_pool.Retire((leaf*) rc);
          return true;
        } else { // internal node
          K& k = p->keys[li];
          // lock both children
          return lc->lck.try_lock([=] {
            return rc->lck.try_lock([=] {
              if (lc->size + rc->size < node_join_cutoff)   // join
                gp->children[pidx] = join_children(p, join(lc, k, rc), li);
              else // rebalance
                gp->children[pidx] = rebalance_children(p, rebalance(lc, k, rc), li);
              lc->removed = rc->removed = p->removed = true;
              node_pool.Retire(p);
              node_pool.Retire(lc);
              node_pool.Retire(rc);
              return true;});});
        }});});
  }

  static void fix_node(node* gp, int pidx, node* p, int cidx, node* c) {
    if (c->status == isOver) overfull_node(gp, pidx, p, cidx, c);
    else underfull_node(gp, pidx, p, cidx, c);
  }

#ifdef Recorded_Once
  // only used for recorded once version
  static node* copy_node_or_leaf(node* p) {
    if (p->is_leaf) {
      leaf* l = (leaf*) p;
      leaf* r = leaf_pool.New(l->size, 0,
                              [=] (int i) { return l->keyvals[i];});
      leaf_pool.Retire(l);
      return (node*) r;
    } else {
      node* r = node_pool.New(p->size,
                              [=] (int i) {return p->keys[i];},
                              [=] (int i) {return p->children[i].load();});
      p->removed = true;
      node_pool.Retire(p);
      return r;
    }
  }
#endif

  // If c (child of root) is overfull it splits it and creates a new
  // internal node with the two split nodes as children.
  // If c is underfull (degree 1), it removes c
  // In both cases it updates the root to point to the new internal node
  // it takes a lock on the root
  static void fix_root(node* root, node* c) {
    root->lck.try_lock([=] {
      // check that c has not changed
      if (!verlib::validate([&] {return root->children[0].load() == c;}))
        return false;
      if (c->status == isOver) {
        if (c->is_leaf) {
          root->children[0] = node_pool.New(split_leaf(c));
          leaf_pool.Retire((leaf*) c);
        } else {
          root->children[0] = node_pool.New(split(c));
          node_pool.Retire(c);
        }
        return true;
      } else { // c has degree 1 and not a leaf
#ifdef Recorded_Once
        // if recorded once then child of c needs to be copied
        return c->lck.try_lock([=] {
          root->children[0] = copy_node_or_leaf(c->children[0].load());
          node_pool.Retire(c);
          return true;});
#else
        // if not recorded once then can be updated in place
        root->children[0] = c->children[0].load();
        node_pool.Retire(c);
        return true;
#endif
      }});
  }

  // Finds the leaf containing a given key, returning the parent, the
  // location of the pointer in the parent to the leaf, and the leaf.
  // Along the way if it encounters any underfull or overfull nodes
  // (internal or leaf) it fixes them and starts over (splitting
  // overfull nodes, and joining underfull nodes with a neighbor).
  // This ensures that the returned leaf is not full, and that it is safe
  // to split a child along the way since its parent is not full and can
  // absorb an extra pointer
  static std::tuple<node*, int, leaf*> find_and_fix(node* root, const K& k) {
    int cnt = 0;
    while (true) {
      node* p = root;
      int cidx = 0;
      node* c = p->children[cidx].load();
      if (c->status == isOver || (!c->is_leaf && c->size == 1))
        fix_root(root, c);
      else while (true) {
          if (c->is_leaf) return std::tuple(p, cidx, (leaf*) c);
          node* gp = p;
          int pidx = cidx;
          p = c;
          cidx = c->find(k);
          c = p->children[cidx].load();
          // The following two lines are only useful if hardware
          // prefetching is turned off.  They prefetch the next two
          // cache lines.
          __builtin_prefetch (((char*) c) + 64); 
          __builtin_prefetch (((char*) c) + 128);
          if (c->status != OK) {
            fix_node(gp, pidx, p, cidx, c);
            if (cnt++ == 100000000) {
              std::cout << "probable infinite loop in find_and_fix" << std::endl;
              abort();
            }
            break;
          }
        }
    }
  }

  static std::tuple<node*, int, leaf*> find_no_fix(node* root, const K& k) {
    node* p = root;
    int cidx = 0;
    node* c = p->children[cidx].load();
    while (!c->is_leaf) {
      __builtin_prefetch (((char*) c) + 64); 
      __builtin_prefetch (((char*) c) + 128);
      cidx = c->find(k);
      p = c;
      c = c->children[cidx].load();
    }
    return std::tuple(p, cidx, (leaf*) c);
  }

  // Inserts by finding the leaf containing the key, and inserting
  // into the leaf.  Since the find ensures the leaf is not full,
  // there will be space for the new key.  It needs a lock on the
  // parent and in the lock needs to check the parent has not been
  // deleted and the child has not changed.  If the try_lock fails it
  // tries again.
  // returns false and does no update if already in tree
  bool insert_(const K& k, const V& v) {
    return flck::try_loop([&] {return try_insert(k, v);});}
  
  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  bool upsert_(const K& k, const V& v) {
    return flck::try_loop([&] {return try_insert(k, v, true);});}
  
  bool upsert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return upsert_(k, v);}); }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }

  std::optional<bool> try_insert(const K& k, const V& v, const bool upsert=false) {
    auto [p, cidx, l] = find_and_fix(root, k);
    int idx = l->find(k);
    if (!upsert && idx != -1) { // already there
      if (p->lck.read_lock([&] { return verlib::validate([&] {
        return (p->children[cidx].load() == (node*) l) && !p->removed.load();});}))
        return false;
      else return {};
    }
    else if (p->lck.try_lock([=] {
      if (!verlib::validate([&] {return !p->removed.load() && (leaf*) p->children[cidx].load() == l;}))
        return false;
      p->children[cidx] = (node*) insert_leaf(l, k, v, upsert && idx != -1);
      leaf_pool.Retire(l);
      return true;})) return !upsert || idx == -1;
    else return {};
  }

  // Deletes by finding the leaf containing the key, and removing from
  // the leaf.  It needs a lock on the parent.  In the lock it needs to
  // check the parent has not been deleted and the child has not
  // changed.  If the try_lock fails it tries again.  Returns false if
  // not found.
  bool remove_(const K& k) {
    return flck::try_loop([&] {return try_remove(k);});}

  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<bool> try_remove(const K& k) {
    auto [p, cidx, l] = find_and_fix(root, k);
    int idx = l->find(k);
    if (idx == -1) // not there
      if (p->lck.read_lock([&] { return verlib::validate([&] {
        return (p->children[cidx].load() == (node*) l) && !p->removed.load();});}))
        return false;
      else return {};
    else if (p->lck.try_lock([=] {
      if (!verlib::validate([&] {return !p->removed.load() && (leaf*) p->children[cidx].load() == l;}))
        return false;
      p->children[cidx] = (node*) remove_leaf(l, k);
      leaf_pool.Retire(l);
      l->keyvals[idx].value.retire();
      return true;})) return true;
    else return {};
  }

  template<typename AddF>
  static bool range_internal(node* a, AddF& add, const K& start, const K& end) {
    while (true) {
      if (a->is_leaf) {
        leaf* la = (leaf*) a;
        int s = la->prev(start, 0);
        int e = la->prev(end, s);
        for (int i = s; i < e; i++) {
          add(la->keyvals[i].key, la->keyvals[i].value.get_value());
#ifdef LazyStamp
          if (verlib::aborted) return false;
#endif
        }
        return true;
      }
      int s = a->find(start);
      int e = a->find(end, s);
      if (s == e) a = a->children[s].read_snapshot();
      else {
        for (int i = s; i <= e; i++) {
          if (!range_internal(a->children[i].read_snapshot(), add, start, end))
            return false;
        }
        return true;
      }
    }
  }

  template<typename AddF>
  void range_(AddF& add, const K& start, const K& end) {
    range_internal(root, add, start, end);
  }

  std::optional<std::optional<V>> try_find(const K& k) {
    using ot = std::optional<std::optional<V>>;
    auto [p, cidx, l] = find_no_fix(root, k);
    if (p->lck.read_lock([&] {return verlib::validate([&] {
      return (p->children[cidx].load() == (node*) l) && !p->removed.load();});}))
      //p->children[cidx].validate();
      //p->removed.validate();
      return ot(l->find_value(k));
    else return ot();
  }

  std::optional<V> find_locked(const K& k) {
    return flck::try_loop([&] {return try_find(k);});
  }

  // a wait-free version that does not split on way down
  std::optional<V> find_(const K& k) {
    auto [p, cidx, l] = find_no_fix(root, k);
    return ((leaf*) l)->find_value(k);
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] {return find_(k);});
  }

  // An empty tree is an empty leaf along with a root pointing tho the
  // leaf.  The root will always contain a single pointer.
  btree() : root(node_pool.New(leaf_pool.New(0))) {
    // std::cout << "key size: " << sizeof(K) << std::endl;
    // std::cout << "value size: " << sizeof(V) << std::endl;
  }
  btree(size_t n) : root(node_pool.New(leaf_pool.New(0))) {
    // std::cout << "key size: " << sizeof(K) << std::endl;
    // std::cout << "value size: " << sizeof(V) << std::endl;
  }

  static void retire_recursive(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) {
      leaf* pl = (leaf*) p;
      for (int i=0; i < pl->size; i++)
	pl->keyvals[i].value.retire();
      leaf_pool.Retire(pl);
    } else {
      parlay::parallel_for(0, p->size, [&] (size_t i) {
	  retire_recursive(p->children[i].load());},1,true);
      node_pool.Retire(p);
    }
  }

  ~btree() {retire_recursive(root);}

  double total_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
      if (p->is_leaf) return depth * ((leaf*) p)->size;
      return parlay::reduce(parlay::tabulate(p->size, [&] (size_t i) {
            return hrec(p->children[i].load(), depth + 1);}));
    };
    return hrec(root, 0);
  }

  using rtup = std::tuple<K,K,long>;

  static rtup check_recursive(node* p, bool is_root) {
    if (p->is_leaf) {
      leaf* l = (leaf*) p;
      K minv = l->keyvals[0].key;
      K maxv = l->keyvals[0].key;
      for (int i=1; i < l->size; i++) {
        if (less(l->keyvals[i].key, minv)) minv = l->keyvals[i].key;
        if (less(maxv, l->keyvals[i].key)) maxv = l->keyvals[i].key;
      }
      return rtup(minv, maxv, l->size);
    }
    if (!is_root && p->size < node_min_size) {
      std::cout << "size " << (int) p->size
                << " too small for internal node" << std::endl;
      abort();
    }
    auto r = parlay::tabulate(p->size, [&] (size_t i) -> rtup {
      return check_recursive(p->children[i].load(), false);});
    long total = parlay::reduce(parlay::map(r, [&] (auto x) {
      return std::get<2>(x);}));
    parlay::parallel_for(0, p->size - 1, [&] (size_t i) {
        if (!less(std::get<1>(r[i]), p->keys[i]) ||
            less(std::get<0>(r[i+1]), p->keys[i])) {
          std::cout << "keys not ordered" << std::endl;
          // std::cout << "keys not ordered around key: " << p->keys[i]
          // << " max before = " << std::get<1>(r[i])
          // << " min after = " << std::get<0>(r[i+1]) << std::endl;
          abort();
        }});
    return rtup(std::get<0>(r[0]),std::get<1>(r[r.size()-1]), total);
  }

  long check(bool verbose=false) {
    auto [minv, maxv, cnt] = check_recursive(root->children[0].load(), true);
    if (verbose) std::cout << "average height = "
                           << ((double) total_height() / cnt)
                           << std::endl;
    return cnt;
  }

  long size() {return check(); }

  void print() {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
      if (p == nullptr) return;
      if (p->is_leaf) {
        leaf* l = (leaf*) p;
        for (int i=0; i < l->size; i++) 
          std::cout << l->keyvals[i].key << ", ";
      } else {
        for (int i=0; i < p->size; i++) {
          prec((p->children[i]).load());
        }
      }
    };
    prec(root);
    std::cout << std::endl;
  }

  static void clear() {
    node_pool.clear();
    leaf_pool.clear();
    value_pool.clear();
  }

  static void reserve(size_t n) {
    //node_pool.reserve(n);
    //leaf_pool.reserve(n);
    //if (use_indirect) value_pool.reserve(n);
  }

  static void shuffle(size_t n) {
    //node_pool.shuffle(n/8);
    //leaf_pool.shuffle(n/8);
  }

  static void stats() {
    node_pool.stats();
    leaf_pool.stats();
    if (use_indirect) value_pool.stats();
  }

};

template <typename K, typename V, typename C>
verlib::memory_pool<typename btree<K,V,C>::node> btree<K,V,C>::node_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename btree<K,V,C>::leaf> btree<K,V,C>::leaf_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename btree<K,V,C>::V> btree<K,V,C>::value_pool;

} // end namespace verlib

#endif // VERLIB_BTREE_H_
