#include <transactions.h>
#include "rebalance.h"
#include "parlay/parallel.h"
#include "parlay/utilities.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <tuple>

template <typename K_,
	  typename V_,
	  typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;
  using Balance = Rebalance<ordered_map<K,V,Compare>>;
  
  static constexpr auto less = Compare{};
  static unsigned long hash(const K& k) {
    return parlay::hash<K>{}(k); }
  static bool equal(const K& a, const K& b) {
    return !less(a,b) && !less(b,a); }

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

  struct head : stm::versioned {
    bool is_leaf;
    bool is_sentinal; // only used by leaf
    stm::atomic_bool removed = false;
    head(bool is_leaf, bool is_sentinal)
      : is_leaf(is_leaf), is_sentinal(is_sentinal) {}
  };

  // mutable internal node
  struct alignas(32) node : head {
    K key;
    stm::atomic<node*> left;
    stm::atomic<node*> right;
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
      l = p_left ? (p->left).load_unprotected() : (p->right).load_unprotected();
    }
    if (gp) {
      gp->removed.validate();
      if (gp_left) gp->left.validate();
      else gp->right.validate();
    }
    if (p_left) p->left.validate();
    else p->right.validate();
    return std::make_tuple(gp, gp_left, p, p_left, l);
  }

  // inserts a key into a leaf.  If a leaf overflows (> block_size) then
  // the leaf is split in the middle and parent internal node is created
  // to point to the two leaves.  
  bool insert(const K& k, const V& v, bool upsert = false) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    auto ptr = p_left ? &(p->left) : &(p->right);
    leaf* old_l = (leaf*) l;
    bool present = old_l->find(k).has_value();
    if (present && !upsert) return false;

    // if the leaf is the left sentinal then create new node
    if (old_l->is_sentinal) {
      leaf* new_l = stm::New<leaf>(KV(k,v));
      (*ptr) = stm::New<node>(k, l, (node*) new_l);
      return true;
    }

    // create new_node by copying old
    leaf* new_l = stm::New<leaf>(old_l->keyvals, old_l->size, k, v, present);

    // if the block overlflows, split into two blocks and
    // create a parent
    if (new_l->size > block_size) {
      int size_l = new_l->size/2;
      int size_r = new_l->size - size_l;
      leaf* new_ll = stm::New<leaf>(new_l->keyvals, size_l);
      leaf* new_lr = stm::New<leaf>(new_l->keyvals + size_l, size_r);
      (*ptr) = stm::New<node>(new_l->keyvals[size_l].key,
                              (node*) new_ll, (node*) new_lr);
      stm::Delete(new_l);
    } else (*ptr) = (node*) new_l;

    // retire the old block
    stm::Delete(old_l);
    Balance::rebalance(p, root, k);
    return !present;
  }

  // Removes a key from the leaf.  If the leaf will become empty by
  // removing it, then both the leaf and its parent need to be deleted.
  bool remove(const K& k) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    leaf* old_l = (leaf*) l;
    auto ptr = p_left ? &(p->left) : &(p->right);
    bool present = old_l->find(k).has_value();
    if (!present) return false;

    // The leaf has at least 2 keys, so the key can be removed from the leaf
    if (old_l->size > 1) {
      // update parent to point to new leaf, and retire old
      (*ptr) = (node*) stm::New<leaf>(old_l->keyvals, old_l->size, k);
      stm::Delete(old_l);
    } else {
      // The leaf has 1 key. We need to delete the leaf (l) and
      // its parent (p), and point the granparent (gp) to the
      // other child of p.

      auto gptr = gp_left ? &(gp->left) : &(gp->right);
      node* ll = (p->left).load();
      node* lr = (p->right).load();
      if (p_left) std::swap(ll,lr);
      if (lr != l) return false; // if l has changed then exit
      p->removed = true;
      (*gptr) = ll; // shortcut
      stm::Delete(p);
      stm::Delete((leaf*) l);
      return true;
    }
    return true;
  }

  std::optional<V> find(const K& k) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    leaf* ll = (leaf*) l;
    for (int i=0; i < ll->size; i++) 
      if (equal(ll->keyvals[i].key, k)) return ll->keyvals[i].value;
    return {};
  }

  node* empty() { return stm::New<node>((node*) stm::New<leaf>()); }
  ordered_map() : root(empty()) {}
  ordered_map(size_t n) : root(empty()) {}
  ~ordered_map() { Retire(root);}
  
  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) stm::Delete((leaf*) p);
    else {
      parlay::par_do([&] () { Retire((p->left).load()); },
		     [&] () { Retire((p->right).load()); });
      stm::Delete(p);
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
             //Balance::check_balance(p, l, r);
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
};

