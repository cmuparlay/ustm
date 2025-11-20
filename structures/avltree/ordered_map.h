// Implementation of an AVL tree to be used transactionally.
// Maintains strict AVL balance (i.e. heights of children differ by at most once).

#include <transactions.h>
#include <parlay/parallel.h>
#include <algorithm>
#include <functional>
#include <iostream>
#include <optional>
#include <tuple>

namespace parlay {
  
template <typename K_,
	  typename V_,
	  typename Compare = std::less<K_>>
struct ordered_map {
  using K = K_;
  using V = V_;
private:
  struct node : stm::versioned {
    const K key;
    const V value;
    stm::atomic<node*> left;
    stm::atomic<node*> right;
    stm::atomic_bool removed = false;
    stm::atomic<int> height;
    static int getHeight(node* p) {
      if (p == nullptr) return 0;
      return p->height.load();
    }
    node(const K& k, const V& v, node* l, node* r)
      : key(k), value(v), left(l), right(r),
        height(1 + std::max(getHeight(l), getHeight(r))) {}
  };

  stm::atomic<node*> root = nullptr;
    
  static void Delete(node* p) {
    p->removed = true;
    stm::Delete(p);
  }

  static node* New(const K& k, const V& v, node* l, node* r) {
    return stm::New<node>(k, v, l, r);
  }

  static void retire_recursive(node* p) {
    if (p == nullptr) return;
    else {
      parlay::par_do([&] () { retire_recursive((p->left).load()); },
		     [&] () { retire_recursive((p->right).load()); });
      Delete(p);
    }
  }

  static auto maxEntry(node* p) { // traverse the right spine to a leaf
    node* r = p->right.load();
    if (r == nullptr) return std::pair<K,V>(p->key, p->value);
    else return maxEntry(r);
  }

  static node* rotateRight(node* p) {
    node* c = p->left.load();
    node* new_c = New(p->key, p->value, c->right.load(), p->right.load());
    node* new_p = New(c->key, c->value, c->left.load(), new_c);
    Delete(c); Delete(p);
    return new_p;
  }

  static node* rotateLeft(node* p) {
    node* c = p->right.load();
    node* new_c = New(p->key, p->value, p->left.load(), c->left.load());
    node* new_p = New(c->key, c->value, new_c, c->right.load());
    Delete(c); Delete(p);
    return new_p;
  }

  // p is parent and l and r are the left and right children.
  // To keep the code clean it does more allocations than needed.
  static node* rebalance(node* p, node* l, node* r) {
    int balance = node::getHeight(l) - node::getHeight(r);
    if (balance > 1) { // left heavy
      if (node::getHeight(l->left.load()) < node::getHeight(l->right.load()))  
        l = rotateLeft(l); // double rotation
      node* new_p = New(p->key, p->value, l, r);
      return rotateRight(new_p);
    } else if (balance < -1) { // right heavy
      if (node::getHeight(r->right.load()) < node::getHeight(r->left.load()))  
        r = rotateRight(r); // double rotation
      node* new_p = New(p->key, p->value, l, r);
      return rotateLeft(new_p);
    } else return p; // balanced
  }

  static std::pair<bool, node*>
  insert(node* p, const K& k, const V& v, bool upsert, bool& inserted) {
    if (p == nullptr) // if empty
      return std::pair(true, New(k, v, nullptr, nullptr));

    node *l, *r, *new_l, *new_r;
    bool changed;
    if (Compare{}(k, p->key)) { // go left
      l = p->left.load();
      std::tie(changed, new_l) = insert(l, k, v, upsert, inserted);
      if (!changed) return std::pair(false, p);
      new_r = r = p->right.load();
    } else if (Compare{}(p->key,k)) { // go right
      r = p->right.load();
      std::tie(changed, new_r) = insert(r, k, v, upsert, inserted);
      if (!changed) return std::pair(false, p);
      new_l = l = p->left.load();
    } else { // key already in tree
      inserted = false;
      if (upsert) {
        Delete(p);
        return std::pair(true, New(k, v, nullptr, nullptr));
      } else return std::pair(false, p);
    }

    node* new_p = rebalance(p, new_l, new_r);
    if (new_p == p) {
      if (new_l == l) p->right = new_r;
      else p->left = new_l;
      int new_height = 1 + std::max(node::getHeight(new_l), node::getHeight(new_r));
      if (new_height != p->height.load()) {
        p->height = new_height;
        return std::pair(true, p);
      } else return std::pair(false, p); 
    } else return std::pair(true, new_p);
  }

  static std::pair<bool,node*> remove(node* p, const K& k, bool& removed) {
    if (p == nullptr) { // if empty
      removed = false;
      return std::pair(false, nullptr);
    }

    node *l, *r, *new_l, *new_r;
    node* new_p = p;
    bool changed;
    if (k == p->key) { // found the key
      new_l = l = p->left.load();
      new_r = r = p->right.load();
      // if leaf
      if (l == nullptr && r == nullptr) { 
        Delete(p);
        return std::pair(true, nullptr);
      }
      // if has 2 children
      else if (l != nullptr && r != nullptr) {
        auto [key, value] = maxEntry(l);
        std::tie(changed, new_l) = remove(l, key, removed);
        new_p = New(key, value, new_l, r);
        Delete(p);
      }
      // if has one child
      else if (r == nullptr) {
        Delete(p);
        return std::pair(true, l);
      } else {
        Delete(p);
        return std::pair(true, r);
      }
    } else { // not equal to key
      if (Compare{}(k, p->key)) { // go left
        l = p->left.load();
        std::tie(changed, new_l) = remove(l, k, removed);
        if (!changed) return std::pair(false, p); // common case
        new_r = r = p->right.load();
      } else { // go right
        r = p->right.load();
        std::tie(changed, new_r) = remove(r, k, removed);
        if (!changed) return std::pair(false, p); // common case
        new_l = l = p->left.load();
      }
    }

    // children have changed, need to check for balance
    node* newer_p = rebalance(new_p, new_l, new_r);
    if (newer_p == p) { 
      if (new_l == l) p->right = new_r;
      else p->left = new_l;
      int new_height = 1 + std::max(node::getHeight(new_l), node::getHeight(new_r));
      if (p->height.load() != new_height) {
        p->height = new_height;
        return std::pair(true, p);
      } else return std::pair(false, p);
    } else return std::pair(true, newer_p);
  }

public:
  ordered_map(long n) {}; 
  ordered_map() {} 
  ~ordered_map() {retire_recursive(root.load());}

  bool remove(const K& key) {
    bool removed = true;
    node* r = root.load();
    auto [changed, new_r] = remove(r, key, removed);
    if (changed) root = new_r;
    return removed;
  }

  long tick = 0;
  
  bool insert(const K& key, const V& value) {
    bool inserted = true;
    node* r = root.load();
    auto [changed, new_r] = insert(r, key, value, false, inserted);
    if (changed) root = new_r;
    //if (tick++ % 10000 == 0)
    //    std::cout << "insert max height = " << max_height() << std::endl;
    return inserted;
  }

  bool upsert(const K& key, const V& value) {
    bool inserted = true;
    node* r = root.load();
    auto [changed, new_r] = insert(r, key, value, true, inserted);
    if (changed) root = new_r;
    // if (tick++ % 10000 == 0)
    //  std::cout << "upsert max height = " << max_height() << std::endl;
    return inserted;
  }

  template <typename F>
  bool upsert_f(const K& k, const F& f) { return upsert(k,f(find(k))); }

  std::optional<V> find(const K& key) {
    node* p = nullptr;
    node* c = root.load_unprotected(); 
    while (c != nullptr && c->key != key) {
      p = c;
      c = ((Compare{}(key, c->key)) ? c->left.load_unprotected() :
           c->right.load_unprotected());
    }
    if (p != nullptr) {
      p->removed.validate();
      if (Compare{}(key, p->key)) p->left.validate();
      else p->right.validate();
    }
    if (c == nullptr) return {};
    else return c->value;
  }

  long size() {
    std::function<long(node*)> crec;
    crec = [&] (node* p) {
             if (p == nullptr) return 0l;
             else {
               long lc, rc;
               parlay::par_do([&] { lc = crec(p->left.load()); },
                              [&] { rc = crec(p->right.load()); });
               return lc + rc + 1;
             }
           };
    long cnt = crec(root.load());
    //std::cout << "average height = " << ((double) total_height() / cnt) << std::endl;
    return cnt;
  }
    
  long check() { return size();}

  // return total height
  double total_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
      if (p == nullptr) return depth;
      size_t d1, d2;
      parlay::par_do([&] () { d1 = hrec((p->left).load(), depth + 1);},
                     [&] () { d2 = hrec((p->right).load(), depth + 1);});
      return d1 + d2;
    };
    return hrec(root.load(), 0);
  }

  // return max height
  double max_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
      if (p == nullptr) return depth;
      size_t d1, d2;
      parlay::par_do([&] () { d1 = hrec((p->left).load(), depth + 1);},
                     [&] () { d2 = hrec((p->right).load(), depth + 1);});
      return std::max(d1, d2);
    };
    return hrec(root.load(), 0);
  }
};
} //end namespace parlay
