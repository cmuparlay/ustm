#ifndef VERLIB_AVLTREE_H_
#define VERLIB_AVLTREE_H_

#include <verlib/verlib.h>
#include <limits>
#include <algorithm>
#include <cstdlib>
#include <parlay/parallel.h>

// no parent pointers, search to key, doesn't work likely because
// violations are getting rotated off search path on read only
// workloads, it is faster than leaftree and natarajan but slower than
// bronson, probably because it is external.  TODO: check that bronson
// has lower average height than this tree inserts and removes are
// very slow, probably due to the unoptimized rotation code.

// why is this AVL tree significantly less balanced than a red-black tree?

// TODO: minimize number of writes to the log

namespace verlib{
  
template <typename K_,
	  typename V_,
	  typename Compare = std::less<K_>>
struct avltree {
  using K = K_;
  using V = V_;

  struct head : verlib::versioned {
    K key;
    bool is_leaf;
    bool is_sentinal;
    head(K key, bool is_leaf)
      : key(key), is_leaf(is_leaf), is_sentinal(false) {}
    head(bool is_leaf)
      : is_leaf(is_leaf), is_sentinal(true) {}
  };

  struct node : head, verlib::lock {
    verlib::atomic_bool removed;
    verlib::versioned_ptr<node> left;
    verlib::versioned_ptr<node> right;
    flck::atomic<int32_t> lefth; // TODO: see if you can get away with storing 1 height
    flck::atomic<int32_t> righth;
    node(K k, node* left, node* right, int32_t lefth, int32_t righth)
      : head(k, false), left(left), right(right), lefth(lefth), righth(righth), removed(false) {};
    node(node* left)
      : head(false), left(left), right(nullptr), lefth(1), righth(0), removed(false) {};
  };

  struct leaf : head {
    V value;
    leaf(K k, V v) : head(k, true), value(v) {};
    leaf() : head(true) {};
  };

  static bool Less(const K& k, node* a) {
    return !a->is_sentinal && Compare{}(k, a->key);
  }

  static bool Equal(const K& k, node* a) {
    return !(a->is_sentinal || Compare{}(k, a->key) || Compare{}(a->key ,k));
  }

  node* root;

  static int32_t height(node* n) {
    if (n == nullptr) abort(); // Guy : fails here on hybrid
    if(n->is_leaf) return 1;
    return 1 + std::max(n->lefth.load(), n->righth.load());
  }

  static verlib::memory_pool<node> node_pool;
  static verlib::memory_pool<leaf> leaf_pool;

  static constexpr size_t max_iters = 10000000;
  
  static auto find_location(node* root, const K& k) {
    int cnt = 0;
    node* gp = nullptr;
    bool gp_left = false;
    node* p = root;
    bool p_left = true;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      if (cnt++ > max_iters) {std::cout << "too many iters" << std::endl; abort();}
      gp = p;
      gp_left = p_left;
      p = l;
      p_left = Less(k, p);
      l = p_left ? (p->left).load() : (p->right).load();
    }
    return std::make_tuple(gp, gp_left, p, p_left, l);
  }

  static bool correctHeight(node* n) {
    if(n->is_leaf) return true;
    return n->lefth.load() == height(n->left.load()) && 
           n->righth.load() == height(n->right.load());
  }

  static int32_t balance(node* n) {
    if(n->is_leaf) return 0;
    return n->lefth.load() - n->righth.load();
  }

  static bool noViolations(node* n) {
    return std::abs(balance(n)) <= 1;
  }

  static void fixHeight(node* n) {
    n->try_lock([=] {
      if(n->removed.load()) return false;
      n->lefth = height(n->left.load());
      n->righth = height(n->right.load());
      return true;
    });
  }

  // remember to retire nodes in the rotate methods.

  static void rotate(node* p, node* n, node* l, bool rotateRight) {
    bool p_left = (p->left.load() == n);
    p->try_lock([=] () {
      if(p->removed.load() || n != (p_left ? p->left.load() : p->right.load())) return false;
      return n->try_lock([=] () {
        if(n->removed.load() || !correctHeight(n) || l != (rotateRight ? n->left.load() : n->right.load()) || (rotateRight && balance(n) < 2) || (!rotateRight && balance(n) > -2)) return false;
        return l->try_lock([=] () {
          if(l->removed.load() || (rotateRight && balance(l) < 0) || (!rotateRight && balance(l) > 0)) return false;
          node* new_n, *new_l;
          if(rotateRight) {
            new_n = node_pool.New(n->key, l->right.load(), n->right.load(), l->righth.load(), n->righth.load());
            new_l = node_pool.New(l->key, l->left.load(), new_n, l->lefth.load(), height(new_n));
          } else {
            new_n = node_pool.New(n->key, n->left.load(), l->left.load(), n->lefth.load(), l->lefth.load());
            new_l = node_pool.New(l->key, new_n, l->right.load(), height(new_n), l->righth.load());
          }
          if(p_left) p->left = new_l;
          else p->right = new_l;
          n->removed = true; node_pool.Retire(n);
          l->removed = true; node_pool.Retire(l);
          return true;
        });
      });
    });
  }

  static void doubleRotate(node* p, node* n, node* l, bool rotateLR) {
    bool p_left = (p->left.load() == n);
    p->try_lock([=] () {
      if(p->removed.load() || n != (p_left ? p->left.load() : p->right.load())) return false;
      return n->try_lock([=] () {
        if(n->removed.load() || !correctHeight(n) || l != (rotateLR ? n->left.load() : n->right.load()) || (rotateLR && balance(n) < 2) || (!rotateLR && balance(n) > -2)) return false;
        return l->try_lock([=] () {
          if(l->removed.load() || !correctHeight(l) || (rotateLR && balance(l) >= 0) || (!rotateLR && balance(l) <= 0)) return false;
          node* cc = rotateLR ? l->right.load() : l->left.load();
          if(cc->is_leaf) return false;
          return cc->try_lock([=] () { 
            if(cc->removed.load()) return false;
            node* new_n, *new_l, *new_cc;
            if(rotateLR) {
              new_n = node_pool.New(n->key, cc->right.load(), n->right.load(), cc->righth.load(), n->righth.load());
              new_l = node_pool.New(l->key, l->left.load(), cc->left.load(), l->lefth.load(), cc->lefth.load());
              new_cc = node_pool.New(cc->key, new_l, new_n, height(new_l), height(new_n));
            } else {
              new_n = node_pool.New(n->key, n->left.load(), cc->left.load(), n->lefth.load(), cc->lefth.load());
              new_l = node_pool.New(l->key, cc->right.load(), l->right.load(), cc->righth.load(), l->righth.load());
              new_cc = node_pool.New(cc->key, new_n, new_l, height(new_n), height(new_l));
            }
            if(p_left) p->left = new_cc;
            else p->right = new_cc;
            n->removed = true; node_pool.Retire(n);
            l->removed = true; node_pool.Retire(l);
            cc->removed = true; node_pool.Retire(cc);
            return true;            
          });
        });
      });
    });
  }

  static void fixViolations(node* p, node* n) {
    // std::cout << "fixed violation" << std::endl;
    if(balance(n) >= 2) {
      node* c = n->left.load();
      if(c->is_leaf) return; // c can only be a leaf of n's height info is out of date
      if(!correctHeight(c)) fixHeight(c);
      if(balance(c) >= 0) 
        rotate(p, n, c, true); // make sure n.lefth is correct before rotating
      else
        doubleRotate(p, n, c, true);
      // else return rotateLeftRight(n); // make sure n.lefth and n.left.righth are correct before rotating
    } else if(balance(n) <= -2) {
      node* c = n->right.load();
      if(c->is_leaf) return; // c can only be a leaf of n's height info is out of date
      if(!correctHeight(c)) fixHeight(c);
      if(balance(c) <= 0) 
        rotate(p, n, c, false); // make sure n.righth is correct before rotating
      else
        doubleRotate(p, n, c, false);
      // else return rotateRightLeft(n); // make sure n.righth and n.right.lefth are correct before rotating
    }
  }

  // // traverses entire subtree and fixes violations
  // // returns whether or not a violation is found
  // bool fixAll(node* p, node* n) {
  //   if(n->is_leaf) return false;
  //   bool violationFound = false;
  //   if(!correctHeight(n)) {
  //     fixHeight(n);
  //     violationFound = true;
  //   }
  //   if(!noViolations(n)) {
  //     fixViolations(p, n);
  //     violationFound = true;
  //   }
  //   if(violationFound) return true;
  //   else return fixAll(n, n->left.load()) || fixAll(n, n->right.load());
  // }

  static void printTreeHelper(node* p, int depth) {
    static int rec[1000006];
    if(p == nullptr) return;
    std::cout << "\t";
    for(int i = 0; i < depth; i++)
        if(i == depth-1)
            std::cout << (rec[depth-1]?"\u0371":"\u221F") << "\u2014\u2014\u2014";
        else
            std::cout << (rec[i]?"\u23B8":"  ") << "   ";
    if (p->is_sentinal) std::cout << " -inf";
    else std::cout << p->key;
    if(p->is_leaf) {
        std::cout << std::endl;
        return;
    }
    if(p->removed.load()) std::cout << "'";
    //if(p->lck != nullptr) std::cout << "L";
    std::cout << " (" << p->lefth.load() << ", " << p->righth.load() << ")" << std::endl;

    rec[depth] = 1;
    printTreeHelper(p->left.load(), depth+1);
    rec[depth] = 0;
    printTreeHelper(p->right.load(), depth+1);
  }
  
  void printTree() {
    printTreeHelper(root, 0);
  }

  // TODO: check how many times the outer loop gets executed per update operation
  // TODO: optimize to fix more than one violation each traversal
  // correctness: in a single threaded executions, heights are always fixed before violations (sortof)
  static void fixToKey(node* root, const K& k) {
    node *p, *n;
    while(true) {
      p = root;
      n = root->left.load();
      node* nodeWithViolation = nullptr;
      node* parent = nullptr;
      bool heightViolation;
      while(!n->is_leaf) {
        if(!correctHeight(n)) {
          nodeWithViolation = n;
          heightViolation = true;
        }
        else if(!noViolations(n)) {
          nodeWithViolation = n;
          parent = p;
          heightViolation = false;
        }
        p = n;
        n = Less(k, n) ? n->left.load() : n->right.load();
      }
      if(nodeWithViolation == nullptr) break; // no vioaltions found
      if(heightViolation) fixHeight(nodeWithViolation);
      else fixViolations(parent, nodeWithViolation);
      // printTree(root->left.load());
    }
  }
  
  // bool foundViolations = false;

  bool upsert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return upsert_(k, v);}); }

  bool upsert_(const K& k, const V& v) { return insert_(k, v, true); }

  template <typename F>
  bool upsert_f(const K& k, const F& f) {
    return upsert_(k,f(find_(k)));
  }

  bool insert(const K& k, const V& v) {
    return verlib::with_epoch([=] {return insert_(k, v);}); }

  bool insert_(const K& k, const V& v, bool upsert = false) {
    int cnt = 0;
    while (true) {
      auto [gp, gp_left, p, p_left, l] = find_location(root, k);
      auto ptr = p_left ? &(p->left) : &(p->right);
      bool present = Equal(k,l);
      if (present && !upsert)
        if (p->read_lock([&] {return verlib::validate([&] {
            return (ptr->load() == (node*) l) && !p->removed.load();});}))
          return false;
        else continue;
      if (p->try_lock([=] () {
           auto ptr = p_left ? &(p->left) : &(p->right);
           if (p->removed.load() || ptr->load() != l) return false;
           node* new_l = (node*) leaf_pool.New(k, v);
           if (present) {
             (*ptr) = new_l;
             leaf_pool.Retire((leaf*) l);
             return true;
           } else {
             node* new_internal = (Less(k, l) ?
                                   node_pool.New(l->key, new_l, l, 1, 1) :
                                   node_pool.New(k, l, new_l, 1, 1));
             (*ptr) = new_internal;
             return true; }})) {
        fixToKey(root, k);
        return !present;
      }
      if (cnt++ > max_iters) {
        std::cout << "too many iters" << std::endl; abort();
      }
    }
  }
  
  bool remove(const K& k) {
    return verlib::with_epoch([=] { return remove_(k);});}

  bool remove_(const K& k) {
    int cnt = 0;
    while (true) {
      auto [gp, gp_left, p, p_left, l] = find_location(root, k);
      auto ptr = gp_left ? &(gp->left) : &(gp->right);
      if (l->is_sentinal) return false;
      if (!Equal(k, l)) {
        if (gp->read_lock([&] {return verlib::validate([&] {
            return (ptr->load() == p) && !gp->removed.load();});}))
          return false;
        else continue;
      }
      if (gp->try_lock([=] () {
          return p->try_lock([=] () {
            if (gp->removed.load() || ptr->load() != p) return false;
            node* ll = (p->left).load();
            node* lr = (p->right).load();
            if (p_left) std::swap(ll,lr);
            if (lr != l) return false;
            p->removed = true;
            node_pool.Retire(p);
            leaf_pool.Retire((leaf*) l);
            (*ptr) = ll; // shortcut
            return true; });})) {
        fixToKey(root, k);
        return true;
      }
      if (cnt++ > max_iters) {std::cout << "too many iters" << std::endl; abort();}
    }
  }

  std::optional<V> find_(const K& k) {
    node* l = (root->left).load();
    while (!l->is_leaf)
      l = Less(k, l) ? (l->left).load() : (l->right).load();
    auto ll = (leaf*) l;
    if (Equal(k, (node*) ll)) return ll->value; 
    else return {};
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
    if (p->read_lock([&] {return verlib::validate([&] {
      return (ptr->load() == (node*) l) && !p->removed.load();});}))
      return ot(Equal(k, l) ?
                std::optional<V>(((leaf*) l)->value) :
                std::optional<V>());
    return ot();
  }

  avltree() {
    node* l = (node*) leaf_pool.New();
    root = node_pool.New(l);
  }

  avltree(size_t n) { 
    node* l = (node*) leaf_pool.New();
    root = node_pool.New(l);
  }

  ~avltree() { Retire(root);}
  
  void print() {
    node* p = root;
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
       if (p->is_leaf) std::cout << p->key << ", ";
       else {
         prec((p->left).load());
         prec((p->right).load());
       }
     };
    prec(p->left.load());
    std::cout << std::endl;
  }

  static void Retire(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) leaf_pool.Retire((leaf*) p);
    else {
      parlay::par_do([&] () { Retire((p->left).load()); },
         [&] () { Retire((p->right).load()); });
      node_pool.Retire(p);
    }
  }
  
  // return total height
  double total_height() {
    node* p = root;
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
       if (p->is_leaf) return depth;
       size_t d1, d2;
       parlay::par_do([&] () { d1 = hrec((p->left).load(), depth + 1);},
          [&] () { d2 = hrec((p->right).load(), depth + 1);});
       return d1 + d2;
     };
    return hrec(p->left.load(), 1);
  }

  long size() { return check(); }

  long check() {
    using rtup = std::tuple<K, K, long>;
    std::function<rtup(node*,bool)> crec;
    crec = [&] (node* p, bool leftmost) {
      if (p->is_leaf)
        if (leftmost) {
          if (p->is_sentinal) return rtup(p->key, p->key, 0);
          else {
            std::cout << "error, leftmost leaf is not a sentinal" << std::endl;
            abort();
          }
        } else if (p->is_sentinal) {
          std::cout << "error, not leftmost leaf is a sentinal" << std::endl;
          abort();
        } else return rtup(p->key, p->key, 1);
      if (p->is_sentinal) {
        std::cout << "error, interior node is a sentinal" << std::endl;
        abort();
      }
      node* pp = p;
      K lmin,lmax,rmin,rmax;
      long lsum, rsum;
      parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec((pp->left).load(), leftmost);},
                     [&] () { std::tie(rmin,rmax,rsum) = crec((pp->right).load(), false);});
      if ((lsum !=0 && !Less(lmax, pp)) || Less(rmin, pp))
        //std::cout << "out of order key: " << lmax << ", " << pp->key << ", " << rmin << std::endl;
        std::cout << "out of order key" << std::endl;
      if (lsum == 0) return rtup(pp->key, rmax, rsum);
      else return rtup(lmin, rmax, lsum + rsum);
    };
    auto l = (root->left).load();
    auto [minv, maxv, cnt] = crec(l, true);
    //std::cout << "average height = " << ((double) total_height(p) / cnt) << std::endl;
    return cnt;
  }

  static void clear() {
    node_pool.clear();
    leaf_pool.clear();
  }

  static void reserve(size_t n) {
    node_pool.reserve(n);
    leaf_pool.reserve(n);
  }

  static void shuffle(size_t n) { }

  static void stats() {
    node_pool.stats();
    leaf_pool.stats();
  }
  
};

template <typename K, typename V, typename C>
verlib::memory_pool<typename avltree<K,V,C>::node> avltree<K,V,C>::node_pool;

template <typename K, typename V, typename C>
verlib::memory_pool<typename avltree<K,V,C>::leaf> avltree<K,V,C>::leaf_pool;

} // end namespace verlib

#endif // VERLIB_AVLTREE_H_
