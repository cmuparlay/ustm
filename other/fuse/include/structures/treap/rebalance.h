
namespace verlib {
  
template <typename Tree>
struct Rebalance {
  using node = typename Tree::node;
  using K = typename Tree::K;
  static node* new_node(K k, node* l, node* r) {
    return Tree::node_pool.New(k, l, r); }
  static void retire_node(node* x) {
    (&(Tree::node_pool))->Retire(x);
  }
  Rebalance() {}
  
  static size_t priority(node* v) {
    return Tree::hash(v->key);
  }

  // If priority of c (child) is less than p (parent), then it rotates c
  // above p to ensure priorities are in heap order.  Two new nodes are
  // created and gp (grandparent) is updated to point to the new copy of c.
  // The key k is needed to decide the side of p from gp, and c from p.
  static bool fix_priority(node* gp, node* p, node* c, const K& k) {
    return gp->try_lock([=] {
	auto gptr = Tree::Less(k, gp) ? &(gp->left) : &(gp->right);
        if (!verlib::validate([&] {return !gp->removed.load() && gptr->load() == p;}))
          return false;
	return p->try_lock([=] {
      	   bool on_left = Tree::less(k, p->key);
           auto ptr = on_left ? &(p->left) : &(p->right);
           if (!verlib::validate([&] {return ptr->load() == c;}))
             return false;
           return c->try_lock([=] {
	      if (on_left) {  // rotate right to bring left child up
                node* nc = new_node(p->key, c->right.load(), p->right.load());
                (*gptr) = new_node(c->key, c->left.load(), nc);
              } else { // rotate left to bring right child up
                node* nc = new_node(p->key, p->left.load(), c->left.load());
                (*gptr) = new_node(c->key, nc, c->right.load());
              }
              // retire the old copies, which have been replaced
              p->removed = true; Tree::node_pool.Retire(p);
              c->removed = true; retire_node(c);
              return true; });  });  });
  }

  static void fix_path(node* root, const K& k) {
    while (true) {
      node* gp = root;
      node* p = (gp->left).load();
      if (p->is_leaf) return;
      node* c = Tree::less(k, p->key) ? (p->left).load() : (p->right).load();
      while (!c->is_leaf && priority(p) >= priority(c)) {
	gp = p;
	p = c;
	c = Tree::less(k, p->key) ? (p->left).load() : (p->right).load();
      }
      if (c->is_leaf) return;
      fix_priority(gp, p, c, k);
    }
  }

  static void rebalance(node* p, node* root, const K& k) {
    node* c = Tree::Less(k, p) ? (p->left).load() : (p->right).load();
    if (!c->is_leaf) fix_path(root, k);
  }

  static void check_balance(node* p, node* l, node* r) {
    if (!l->is_leaf && priority(l) > priority(p))
      std::cout << "bad left priority: " << priority(l) << ", " << priority(p) << std::endl;
    if (!r->is_leaf && priority(r) > priority(p))
      std::cout << "bad right priority: " << priority(r) << ", " << priority(p) << std::endl;
  }
};

}
