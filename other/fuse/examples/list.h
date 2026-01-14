// An optimistic locking implementation of an ordered linked list using fuse

#include <fuse/fuse.h>
#include <parlay/parallel.h>

template <typename K>
struct ordered_list {

  struct link : fuse::versioned {
    K key;
    fuse::atomic<link*> next;
    fuse::shared_mutex lck;
    // used to mark when a link is spliced out
    fuse::atomic<bool> removed = false;
    link(K key, link* next) : key(key), next(next) {}
  };

  link head = link(K{}, nullptr);

  // traversal without any locks
  auto find_link(const K& k) {
    link* p = &head;
    link* nxt = p->next.load();
    while (nxt != nullptr && nxt->key > k) {
      p = nxt;
      nxt = p->next.load();
    }
    return std::pair(p, nxt);
  }

  bool insert(const K& k) {
    while (true) {                              
      auto [p, nxt] = find_link(k);
      // after the traversal, take a shared lock and validate
      std::shared_lock lck1(p->lck);
      if (p->removed.load() || p->next.load() != nxt)
        continue;
      // Return false if key is already in the list
      if (nxt != nullptr && nxt->key == k)
        return false;
      // If not in the list, upgrade to a unique lock on p and link in
      // the new key
      std::unique_lock lck2(p->lck);
      p->next = fuse::New<link>(k, nxt);
      return true;
    }
  }

  bool remove(const K& k) {
    while (true) {                              
      auto [p, nxt] = find_link(k);
      // after the traversal, take a shared lock and validate
      std::shared_lock lock1(p->lck);
      if (p->removed.load() || p->next.load() != nxt)
        continue;
      // return false if k not in list
      if (nxt == nullptr || nxt->key != k)
        return false;
      // if k is in list, upgrade lock on p to a unique lock, take
      // a unique lock on nxt, and splice out nxt
      std::unique_lock lock2(p->lck);
      std::unique_lock lock3(nxt->lck);
      p->next = nxt->next.load();
      nxt->removed = true;
      fuse::Retire(nxt);
      return true;
    }
  }

  bool find(const K& k) {
    while (true) {
      auto [p, nxt] = find_link(k);
      // after the traversal, take a shared lock and validate
      std::shared_lock lock(p->lck);
      if (p->removed.load() || p->next.load() != nxt)
        continue;
      // if validated then return whether found
      return (nxt != nullptr && nxt->key == k);
    }
  }

  ordered_list() {}
  ~ordered_list() {
    link* nxt = head.next.load();
    while (nxt != nullptr) {
      link* h = nxt;
      nxt = nxt->next.load();
      fuse::Retire(h);
    }
  }
  
  long size() {
    link* nxt = head.next.load();
    long cnt = 0;
    while (nxt != nullptr) {
      nxt = nxt->next.load();
      cnt++;
    }
    return cnt;
  }
};
