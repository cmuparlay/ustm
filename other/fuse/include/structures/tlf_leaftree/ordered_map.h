#ifndef FUSE_LEAFTREE_H
#define FUSE_LEAFTREE_H

#include <fuse/fuse.h>
#include <parlay/parallel.h>

// A concurrent tree with values stored at the leaves and no balancing
// Uses optimistic locking.

// Currently uses strict locks.
// Could use: std::unique_lock lck(p->lck, std::defer_lock);
//            if (!lck.try_lock()) return {};
// instead.

namespace fuse {

template <typename K_,
	  typename V_,
	  typename Less = std::less<K_>>
struct tlf_leaftree_map {
  using K = K_;
  using V = V_;

private:
  struct internal;
  internal* root;

  // Sentinal to mark smaller and larger than any key
  enum sentinal : char {MinSentinal, IsKey, MaxSentinal};
  
  // common header for internal nodes and leaves
  struct node : fuse::versioned {
    K key;
    bool is_leaf;
    sentinal s;
    node(K k, bool l, sentinal s) : key(k), is_leaf(l), s(s) {}
  };
  
  // internal node
  struct internal : node {
    fuse::atomic<bool> removed;
    fuse::shared_mutex lck;
    fuse::atomic<node*> left;
    fuse::atomic<node*> right;
    internal(K k, node* left, node* right)
      : node{k, false, IsKey}, left(left), right(right), removed(false) {};
    internal(node* left) // for the root, only has a left pointer
      : node{K{}, false, MaxSentinal}, left(left), right(nullptr), removed(false) {};
  };

  struct leaf : node {
    V value;
    leaf(K k, V v) : node{k, true, IsKey}, value(v) {};
    leaf() : node{K{}, true, MinSentinal} {}; // for the sentinal leaf
  };

  // Less and equal accounting for sentinals
  static bool less(const K& k, node* a) {
    if (a->s == IsKey) return Less{}(k, a->key);
    return (a->s == MaxSentinal);
  }
  
  static bool equal(const K& k, node* a) {
    return !(a->s != IsKey || Less{}(k, a->key) || Less{}(a->key ,k));
  }

  static fuse::atomic<node*>* get_child(internal* a, const K& k) {
    return less(k, a) ? &(a->left) : &(a->right);}

  // return a grandparent, parent and leaf
  auto find_leaf(const K& k) {
    internal* gp = nullptr;
    internal* p = root;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      gp = p;
      p = (internal*) l;
      l = get_child(p, k)->load();
    }
    return std::make_tuple(gp, p, (leaf*) l);
  }

  long size_recursive(node* p) {
    if (p->is_leaf) return 1l;
    internal* pp = (internal*) p;
    size_t s1, s2;
    parlay::par_do([&] { s1 = size_recursive((pp->left).load());},
                   [&] { s2 = size_recursive((pp->right).load());});
    return s1 + s2;
  }

public:
  bool insert(const K& k, const V& v) {
    return fuse::with_epoch([=] {
    while (true) {                              
      auto [gp, p, l] = find_leaf(k);
      auto ptr = get_child(p, k);
      if (equal(k, l)) return false;
      std::unique_lock lck_p(p->lck);
      if (p->removed.load() || ptr->load() != l)
        continue;
      node* new_l = fuse::New<leaf>(k, v);
      *ptr = (less(k, l) ?
              fuse::New<internal>(l->key, new_l, l) :
              fuse::New<internal>(k, l, new_l));
      return true;
    }});
  }
  
  bool remove(const K& k) {
    return fuse::with_epoch([=] {
    while (true) {
      auto [gp, p, l] = find_leaf(k);
      if (!equal(k, l)) return false;
      auto g_ptr = get_child(gp, k);
      auto p_ptr = get_child(p, k);
      std::unique_lock lck_gp(gp->lck);
      std::unique_lock lck_p(p->lck);
      if (!gp->removed.load() &&
          g_ptr->load() == p &&
          p_ptr->load() == l) {
        *g_ptr = less(k, p) ? (p->right).load() : (p->left).load();
        p->removed = true;
        fuse::Retire<internal>(p);
        fuse::Retire<leaf>(l);
        return true;
      }
    }});
  }

  std::optional<V> find_(const K& k) {
    auto [gp, p, l] = find_leaf(k);
    if (equal(k, l)) return l->value; 
    else return {};
  }

  std::optional<V> find_single(const K& k) {
    return fuse::with_epoch([&] { return find_(k);});}

  std::optional<V> find(const K& k) {
    while (true) {
      auto [gp, p, l] = find_leaf(k);
      auto ptr = get_child(p, k);
      std::shared_lock lck_p(p->lck);
      if ((ptr->load() == (node*) l) && !p->removed.load())
        if (equal(k, l)) return l->value;
        else return {};
    }
  }

  tlf_leaftree_map() : root(fuse::New<internal>(fuse::New<leaf>())) {}
  tlf_leaftree_map(size_t n) : root(fuse::New<internal>(fuse::New<leaf>())) {}
  ~tlf_leaftree_map() { Retire(root);}

  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) fuse::Retire<leaf>((leaf*) p);
    else {
      internal* pp = (internal*) p;
      parlay::par_do([&] () { Retire((pp->left).load()); },
		     [&] () { Retire((pp->right).load()); });
      fuse::Retire<internal>(pp);
    }
  }
  
  long size() {return size_recursive(root->left.load()) - 1; }

};

} // end namespace fuse
#endif
