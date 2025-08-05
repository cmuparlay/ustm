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

  struct internal;
  internal* root;
  
  // common header for internal nodes and leaves
  struct node : stm::versioned  {
    K key;
    bool is_leaf;
    bool is_sentinal;
    node(K key, bool is_leaf)
      : key(key), is_leaf(is_leaf), is_sentinal(false) {}
    node(bool is_leaf) :
      is_leaf(is_leaf), is_sentinal(true) {}
  };

  // internal node
  struct alignas(64) internal : node {
    stm::atomic<node*> left;
    stm::atomic<node*> right;
    stm::atomic_bool removed = false;
    bool check = false;
    internal(K k, node* left, node* right)
      : node{k,false}, left(left), right(right) {};
    internal(node* left) // for the root, only has a left pointer
      : node{false}, left(left), right(nullptr) {};
  };

  struct leaf : node {
    V value;
    leaf(K k, V v) : node{k,true}, value(v) {};
    leaf() : node{true} {}; // for the sentinal leaf
  };

  static bool Less(const K& k, node* a) {
    return !a->is_sentinal && Compare{}(k, a->key);
  }
  static bool Equal(const K& k, node* a) {
    return !(a->is_sentinal || Compare{}(k, a->key) || Compare{}(a->key ,k));
  }

  static auto find_location(internal* root, const K& k) {
    internal* gp = nullptr;
    bool gp_left = false;
    internal* p = root;
    bool p_left = true;
    node* l = (p->left).load();
    while (!l->is_leaf) {
      gp = p;
      gp_left = p_left;
      p = (internal*) l;
      p_left = Less(k, p);
      l = p_left ? (p->left).load_unprotected() : (p->right).load_unprotected();
    }
    if (gp) {
      gp->removed.validate();
      if (gp_left) gp->left.validate();
      else gp->right.validate();
    }
    if (p_left) p->left.validate();
    else p->right.validate();
    return std::make_tuple(gp, gp_left, p, p_left, (leaf*) l);
  }
  
  bool insert(const K& k, const V& v, bool upsert=false) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    auto ptr = p_left ? &(p->left) : &(p->right);
    if (Equal(k, l)) return false;
    node* new_l = stm::New<leaf>(k, v);
    *ptr = (Less(k, l) ?
            stm::New<internal>(l->key, new_l, l) :
            stm::New<internal>(k, l, new_l));
    return true;
  }

  bool upsert(const K& k, const V& v) { return insert(k, v, true); }
      
  bool remove(const K& k) {
    auto [gp, gp_left, p, p_left, l] = find_location(root, k);
    auto ptr = gp_left ? &(gp->left) : &(gp->right);
    if (l->is_sentinal || !Equal(k, l)) return false;
    node* ll = (p->left).load();
    node* lr = (p->right).load();
    if (p_left) std::swap(ll,lr);
    if (lr != l) return false;
    p->removed = true;
    (*ptr) = ll; // shortcut
    stm::Delete(p);
    stm::Delete(l);
    return true;
  }

  std::optional<V> find(const K& k) {
    auto ptr = &(root->left);
    node* l = ptr->load();
    internal* p;
    while (!l->is_leaf) {
      p = (internal*) l;
      ptr = Less(k, p) ? &(p->left) : &(p->right);
      l = ptr->load_unprotected();
    }
    ptr->validate();
    p->removed.validate();
    auto ll = (leaf*) l;
    if (Equal(k, ll)) return ll->value; 
    else return {};
  }

  ordered_map() : root(stm::New<internal>(stm::New<leaf>())) {}
  ordered_map(size_t n) : root(stm::New<internal>(stm::New<leaf>())) {}
  ~ordered_map() {retire_recursive(root);}
    
  static void retire_recursive(node* p) {
    if (p == nullptr) return;
    if (p->is_leaf) stm::Delete((leaf*) p);
    else {
      internal* pp = (internal*) p;
      parlay::par_do([&] () { retire_recursive((pp->left).load()); },
		     [&] () { retire_recursive((pp->right).load()); });
      stm::Delete(pp);
    }
  }
  
  void print() {
    std::function<void(node*)> prec;
    prec = [&] (node* p) {
      if (p->is_sentinal) std::cout << "[";
      else if (p->is_leaf) std::cout << p->key;
      else {
        internal* pp = (internal*) p;
        prec((pp->left).load());
        if (!(pp->left).load()->is_sentinal)
          std::cout << ", ";
        prec((pp->right).load());
      }
    };
    prec(root->left.load());
    std::cout << "]" << std::endl;
  }

  // return total height
  double total_height() {
    std::function<size_t(node*, size_t)> hrec;
    hrec = [&] (node* p, size_t depth) {
      if (p->is_leaf) return depth;
      internal* pp = (internal*) p;
      size_t d1, d2;
      parlay::par_do([&] () { d1 = hrec((pp->left).load(), depth + 1);},
                     [&] () { d2 = hrec((pp->right).load(), depth + 1);});
      return d1 + d2;
    };
    return hrec(root->left.load(), 1);
  }

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
      internal* pp = (internal*) p;
      K lmin,lmax,rmin,rmax;
      long lsum, rsum;
      parlay::par_do([&] () { std::tie(lmin,lmax,lsum) = crec((pp->left).load(), leftmost);},
                     [&] () { std::tie(rmin,rmax,rsum) = crec((pp->right).load(), false);});
      if ((lsum !=0 && !Less(lmax, pp)) || Less(rmin, pp))
        std::cout << "out of order key: " << lmax << ", " << pp->key << ", " << rmin << std::endl;
      if (lsum == 0) return rtup(pp->key, rmax, rsum);
      else return rtup(lmin, rmax, lsum + rsum);
    };
    auto l = (root->left).load();
    auto [minv, maxv, cnt] = crec(l, true);
    //std::cout << "average height = " << ((double) total_height() / cnt) << std::endl;
    return cnt;
  }
  
};
