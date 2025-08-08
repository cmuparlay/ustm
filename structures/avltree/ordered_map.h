// Implementation of an AVL tree to be used transactionally.
// Maintains strict AVL balance (i.e. heights of children differ by at most once).

#include <transactions.h>
#include <parlay/parallel.h>
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

  static node* insert(node* p, const K& k, const V& v, bool upsert, bool& inserted) {
    if (p == nullptr) // if empty
      return New(k, v, nullptr, nullptr);

    node *l, *r, *new_l, *new_r;
    if (k < p->key) { // go left
      l = p->left.load();
      new_l = insert(l, k, v, upsert, inserted);
      if (new_l == l) return p;
      new_r = r = p->right.load();
    } else if (k > p->key) { // go right
      r = p->right.load();
      new_r = insert(r, k, v, upsert, inserted);
      if (new_r == r) return p;
      new_l = l = p->left.load();
    } else { // key already in tree
      if (upsert) {
        Delete(p);
        return New(k, v, nullptr, nullptr);
      }
      inserted = false;
      return p;
    }

    node* new_p = rebalance(p, new_l, new_r);
    if (new_p == p) {
      int new_height = 1 + std::max(node::getHeight(new_l), node::getHeight(new_r));
      if (new_height != p->height.load()) p->height = new_height;
      if (new_l == l) p->right = new_r;
      else p->left = new_l;
      return p;
    } else return new_p;
  }

  static node* remove(node* p, const K& k, bool& removed) {
    if (p == nullptr) { // if empty
      removed = false;
      return nullptr;
    }

    node *l, *r, *new_l, *new_r;
    node* new_p = p;
    if (k == p->key) { // found the key
      new_l = l = p->left.load();
      new_r = r = p->right.load();
      // if leaf
      if (l == nullptr && r == nullptr) { 
        Delete(p);
        return nullptr;
      }
      // if has 2 children
      else if (l != nullptr && r != nullptr) {
        auto [key, value] = maxEntry(l);
        new_l = remove(l, key, removed);
        new_p = New(key, value, new_l, r);
        Delete(p);
      }
      // if has one child
      else if (r == nullptr) {
        Delete(p);
        return l;
      } else {
        Delete(p);
        return r;
      }
    } else { // not equal to key 
      if (k < p->key) { // go left
        l = p->left.load();
        new_l = remove(l, k, removed);
        if (new_l == l) return p; // common case
        new_r = r = p->right.load();
      } else { // go right
        r = p->right.load();
        new_r = remove(r, k, removed);
        if (new_r == r) return p; // common case
        new_l = l = p->left.load();
      }
    }

    // children have changed, need to check for balance
    node* newer_p = rebalance(new_p, new_l, new_r);
    if (newer_p == p) { 
      int new_height = 1 + std::max(node::getHeight(new_l), node::getHeight(new_r));
      if (p->height.load() != new_height) p->height = new_height;
      if (new_l == l) p->right = new_r;
      else p->left = new_l;
      return p;
    } else return newer_p;
  }

public:
  ordered_map(long n) {}; 
  ordered_map() {} 
  ~ordered_map() {retire_recursive(root.load());}

  bool remove(const K& key) {
    bool removed = true;
    node* r = root.load();
    node* new_r = remove(r, key, removed);
    if (r != new_r) root = new_r;
    return removed;
  }

  bool insert(const K& key, const V& value) {
    bool inserted = true;
    node* r = root.load();
    node* new_r = insert(r, key, value, false, inserted);
    if (r != new_r) root = new_r;
    return inserted;
  }

  bool upsert(const K& key, const V& value) {
    bool inserted = true;
    node* r = root.load();
    node* new_r = insert(r, key, value, true, inserted);
    if (r != new_r) root = new_r;
    return inserted;
  }

  template <typename F>
  bool upsert_f(const K& k, const F& f) { return upsert(k,f(find(k))); }

  std::optional<V> find(const K& key) {
    node* p = nullptr;
    node* c = root.load_unprotected(); 
    while (c != nullptr && c->key != key) {
      p = c;
      c = ((key < c->key) ? c->left.load_unprotected() :
           c->right.load_unprotected());
    }
    if (p != nullptr) {
      p->removed.validate();
      if (key < p->key) p->left.validate();
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

};
