#ifndef VERLIB_TREAP_H_
#define VERLIB_TREAP_H_

#include <verlib/verlib.h>
#include "rebalance.h"
#include "parlay/parallel.h"
#include "parlay/utilities.h"
#include <functional>
#include <iostream>
#include <optional>

namespace verlib { 
#ifndef BALANCED
bool balanced = true;
#else
bool balanced = false;
#endif

template <typename K_,
	  typename V_,
	  class Hash = parlay::hash<K_>,
	  typename Compare = std::less<K_>>
struct treap {
  using K = K_;
  using V = V_;
  using Balance = Rebalance<treap<K,V,Hash,Compare>>;
  
  static constexpr auto less = Compare{};
  static constexpr auto hash = Hash{};
  static bool equal(const K& a, const K& b) {
    return !less(a,b) && !less(b,a);
  }

  struct node;
  node* root;

  struct KV {
    K key;
    V value;
    KV(K key, V value) : key(key), value(value) {}
    KV() {}
  };
  static constexpr int block_bytes = 64 * 8;
  static constexpr int block_size = (block_bytes - 16)/sizeof(KV) + 1; 

  struct head : verlib::versioned {
    bool is_leaf;
    bool is_sentinal; // only used by leaf
    verlib::atomic_bool removed = false;
    head(bool is_leaf, bool is_sentinal)
      : is_leaf(is_leaf), is_sentinal(is_sentinal) {}
  };

  // mutable internal node
  struct alignas(32) node : head, verlib::lock {
    K key;
    verlib::versioned_ptr<node> left;
    verlib::versioned_ptr<node> right;
    node(K k, node* left, node* right)
      : key(k), head{false,false}, left(left), right(right) {};
    node(node* left) // root node
      : head{false,true}, left(left), right(nullptr) {};
  };

  static bool Less(const K& k, node* a) {
    return a->is_sentinal || Compare{}(k, a->key);
  }

  // immutable leaf
  struct alignas(64) leaf : head {
    long size;
    KV keyvals[block_size+1];
    std::optional<V> find(const K& k) {
      int mid = (size-1)/2;
      if (less(k, keyvals[mid].key)) { // first half
        for (int i = 0; i < mid; i++) 
          if (equal(keyvals[i].key, k)) return keyvals[i].value;
        } else {
        for (int i = mid; i < size; i++) 
          if (equal(keyvals[i].key, k)) return keyvals[i].value;
        }
      return {};
    }
    leaf() : size(0), head{true,true} {}; // sentinal (leftmost leaf)
    leaf(const KV& keyval) : size(1), head{true,false} {
      keyvals[0] = keyval;}

    // insert or replace key-value into old_keyvals by copying
    leaf(KV* old_keyvals, int n, const K& k, const V& v, bool present): head{true,false} {
      int i=0;
      for (;i < n && less(old_keyvals[i].key, k); i++)
        keyvals[i] = old_keyvals[i];
      keyvals[i] = KV(k,v);
      int offset = present ? 0 : 1;
      for (; i < n; i++) keyvals[i+offset] = old_keyvals[i];
      size = n + offset;
    }

    // copy from old_keyvals
    leaf(KV* old_keyvals, int n): size{n}, head{true,false} {
      for (int i = 0; i < n; i++) keyvals[i] = old_keyvals[i];
    }

    // delete from old_keyvals (k must be present)
    leaf(KV* old_keyvals, int n, const K& k): size{n-1}, head{true,false} {
      int i = 0;
      for (; less(old_keyvals[i].key, k); i++) keyvals[i] = old_keyvals[i];
      for (; i < n - 1; i++ ) keyvals[i] = old_keyvals[i+1];
    }
  };

  static verlib::memory_pool<node> node_pool;
  static verlib::memory_pool<leaf> leaf_pool;

  enum direction {left, right};
  
  static auto find_location(node* root, const K& k) {
    node* gp = nullptr;
    bool gp_left = false;
    node* p = root;
    bool p_left = true;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      gp = p;
      gp_left = p_left;
      p = l;
      p_left = less(k, p->key);
      l = p_left ? (p->left).load() : (p->right).load();
    }
    return std::make_tuple(gp, gp_left, p, p_left, l);
  }

  // inserts a key into a leaf.  If a leaf overflows (> block_size) then
  // the leaf is split in the middle and parent internal node is created
  // to point to the two leaves.  The code within the locks is
  // idempotent.  All changeable values (left and right) are accessed
  // via an atomic_ptr and the pool allocators, which are idempotent.
  // The other values are "immutable" (i.e. they are either written once
  // without being read, or just read).
  bool insert_(const K& k, const V& v, bool upsert = false) {
      while (true) {
	auto [gp, gp_left, p, p_left, l] = find_location(root, k);
        auto ptr = p_left ? &(p->left) : &(p->right);
	leaf* old_l = (leaf*) l;
        bool present = old_l->find(k).has_value();
        if (present && !upsert)
          if (p->read_lock([&] {return verlib::validate([&] {
              return (ptr->load() == l) && !p->removed.load();});}))
            return false;
          else continue;

        if (p->try_lock([=] {
              // if p has been removed, or l has changed, then exit
              if (!verlib::validate([&] {return !p->removed.load() && ptr->load() == l;}))
                return false;
	      // if the leaf is the left sentinal then create new node
	      if (old_l->is_sentinal) {
		leaf* new_l = leaf_pool.New(KV(k,v));
		(*ptr) = node_pool.New(k, l, (node*) new_l);
		return true;
	      }

              // create new_node by copying old
              leaf* new_l = leaf_pool.New(old_l->keyvals, old_l->size, k, v, present);

	      // if the block overlflows, split into two blocks and
	      // create a parent
	      if (new_l->size > block_size) {
		int size_l = new_l->size/2;
		int size_r = new_l->size - size_l;
                leaf* new_ll = leaf_pool.New(new_l->keyvals, size_l);
                leaf* new_lr = leaf_pool.New(new_l->keyvals + size_l, size_r);
		(*ptr) = node_pool.New(new_l->keyvals[size_l].key,
                                       (node*) new_ll, (node*) new_lr);
		leaf_pool.Retire(new_l);
	      } else (*ptr) = (node*) new_l;

	      // retire the old block
	      leaf_pool.Retire(old_l);
	      return true;
	    })) {
	  if (balanced) Balance::rebalance(p, root, k);
	  return !present;
	}
	// try again if unsuccessful
      }
  }

  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  bool upsert_(const K& k, const V& v) { return insert_(k, v, true); }

  bool upsert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return upsert_(k, v);}); }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }

  // Removes a key from the leaf.  If the leaf will become empty by
  // removing it, then both the leaf and its parent need to be deleted.
  bool remove_(const K& k) {
    long cnt = 0;
      while (true) {
	auto [gp, gp_left, p, p_left, l] = find_location(root, k);
	leaf* old_l = (leaf*) l;
        auto ptr = p_left ? &(p->left) : &(p->right);
        bool present = old_l->find(k).has_value();
        if (!present) 
          if (gp->read_lock([&] {return verlib::validate([&] {
               return (ptr->load() == l) && !p->removed.load();});}))
            return false;
          else continue;

        // The leaf has at least 2 keys, so the key can be removed from the leaf
	if (old_l->size > 1) {
	  if (p->try_lock([=] {
                if (!verlib::validate([&] {return !p->removed.load() && ptr->load() == l;}))
                  return false;

		// update parent to point to new leaf, and retire old
		(*ptr) = (node*) leaf_pool.New(old_l->keyvals, old_l->size, k);
		leaf_pool.Retire(old_l);
		return true;
	      }))
	    return true;

	  // 
	} else {
	  // The leaf has 1 key. We need to delete the leaf (l) and
	  // its parent (p), and point the granparent (gp) to the
	  // other child of p.
	  if (gp->try_lock([=] {

              // if gp has been removed, or p has changed, then exit
	      auto gptr = gp_left ? &(gp->left) : &(gp->right);
	      if (!verlib::validate([&] {return !gp->removed.load() && gptr->load() == p;}))
                return false;

	      // lock p and remove p and l
	      return p->try_lock([=] {
		  node* ll = (p->left).load();
		  node* lr = (p->right).load();
		  if (p_left) std::swap(ll,lr);
		  if (lr != l) return false; // if l has changed then exit
		  p->removed = true;
		  (*gptr) = ll; // shortcut
		  node_pool.Retire(p);
		  leaf_pool.Retire((leaf*) l);
		  return true; });}))
	    return true;
	// try again if unsuccessful
        }
      }
  }

  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  std::optional<V> find_(const K& k) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    leaf* ll = (leaf*) l;
    for (int i=0; i < ll->size; i++) 
      if (equal(ll->keyvals[i].key, k)) return ll->keyvals[i].value;
    return {};
  }

  std::optional<V> find(const K& k) {
    return verlib::with_epoch([&] { return find_(k);});
  }

  std::optional<V> find_locked(const K& k) {
    return flck::try_loop([&] {return try_find(k);});}

  std::optional<std::optional<V>> try_find(const K& k) {
    using ot = std::optional<std::optional<V>>;
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    auto ptr = p_left ? &(p->left) : &(p->right);
    leaf* ll = (leaf*) l;
    if (p->read_lock([&] {return verlib::validate([&] {
           return (ptr->load() == (node*) l) && !p->removed.load();});})) {
      for (int i=0; i < ll->size; i++) 
        if (equal(ll->keyvals[i].key, k))
          return std::optional<V>(ll->keyvals[i].value);
      return std::optional<V>();
    } else return ot();
  }

  node* empty() { return node_pool.New((node*) leaf_pool.New()); }
  treap() : root(empty()) {}
  treap(size_t n) : root(empty()) {}
  ~treap() { Retire(root);}
  
  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) leaf_pool.Retire((leaf*) p);
    else {
      parlay::par_do([&] () { Retire((p->left).load()); },
		     [&] () { Retire((p->right).load()); });
      node_pool.Retire(p);
    }
  }

  double total_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
	     if (p->is_leaf) return depth * ((leaf*) p)->size;
	     size_t d1, d2;
	     parlay::par_do([&] () { d1 = hrec((p->left).load(), depth + 1);},
			    [&] () { d2 = hrec((p->right).load(), depth + 1);});
	     return d1 + d2;
	   };
    return hrec(root->left.load(), 1);
  }
  
  long check() {
    using rtup = std::tuple<K,K,long>;
    std::function<rtup(node*)> crec;
    bool bad_val = false;
    crec = [&] (node* p) {
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
	     node* l = p->left.load();
	     node* r = p->right.load();
	     K lmin,lmax,rmin,rmax;
	     long lsum,rsum;
	     parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec(l);},
			    [&] () { std::tie(rmin,rmax,rsum) = crec(r);});
	     if ((lsum > 0 && !less(lmax, p->key)) || less(rmin, p->key)) {
	       std::cout << "bad value: " << std::endl;
               //lmax << ", " << p->key << ", " << rmin << std::endl;
	       bad_val = true;
	     }
             // if (balanced) Balance::check_balance(p, l, r);
	     if (lsum == 0) return rtup(p->key, rmax, rsum);
	     else return rtup(lmin, rmax, lsum + rsum);
	   };
    auto [minv, maxv, cnt] = crec(root->left.load());
    // std::cout << "average height = " << ((double) total_height(p) / cnt) << std::endl;
    return bad_val ? -1 : cnt;
  }

  void print() {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
	     if (p->is_leaf) {
	       leaf* l = (leaf*) p;
	       for (int i=0; i < l->size; i++) 
		 std::cout << l->keyvals[i].key << ", ";
	     } else {
	       prec((p->left).load());
	       prec((p->right).load());
	     }
	   };
    prec(root->left.load());
    std::cout << std::endl;
  }

  static void clear() {
    node_pool.clear();
    leaf_pool.clear();
  }

  static void reserve(size_t n) {
    node_pool.reserve(n/8);
    leaf_pool.reserve(n);
  }

  static void shuffle(size_t n) { }

  static void stats() {
    node_pool.stats();
    leaf_pool.stats();
  }

};

template <typename K, typename V, typename H, typename C>
verlib::memory_pool<typename treap<K,V,H,C>::node> treap<K,V,H,C>::node_pool;

template <typename K, typename V, typename H, typename C>
verlib::memory_pool<typename treap<K,V,H,C>::leaf> treap<K,V,H,C>::leaf_pool;

} // end namespace verlib

#endif // VERLIB_TREAP_H_
