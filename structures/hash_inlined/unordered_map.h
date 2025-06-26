#ifndef PARLAY_HASH_H_
#define PARLAY_HASH_H_

#include <cmath>
#include <functional>
#include <optional>
#include <vector>
#include <verlib/verlib.h>
#include <parlay/primitives.h>

template <typename K_,
	  typename V_,
	  class Hash = std::hash<K_>,
	  class KeyEqual = std::equal_to<K_>>
struct unordered_map {
  using K = K_;
  using V = V_;
  struct KV {K key; V value;};
  
  // buffer_size is picked so state fits in a cache line (if it can)
  static constexpr long buffer_size = (sizeof(KV) > 24) ? 1 : 48 / sizeof(KV);

  // *********************************************
  // The state structure for each bucket
  // *********************************************

  struct alignas(64) state {
    // for overflow lists for each bucket
    struct link {
      KV entry;
      link* next;
      link(const KV& entry, link* next) : entry(entry), next(next) { }
    };

    char cnt = 0;
    using byte = std::byte;
    //byte hashes[7];
    link* next = nullptr;
    KV buffer[buffer_size];

    static link* new_link(const KV& entry, link* l) {
      return ustm::New<link>(entry, l); }
    static void retire_link(link* l) { ustm::Delete(l);}

    // static bool check_not_found(size_t xxx, byte hash) {
    //   int size = xxx & 255;
    //   if (size > buffer_size) return false;
    //   size_t x = xxx >> 8;
    //   byte* hash_copy = (byte*) (&x);
    //   for (long i = 0; i < std::min<long>(size, buffer_size); i++) 
    //     if (hash_copy[i] == hash) return false;
    //   return true;
    // }
      
    std::optional<V> find(const K& key, byte hash) {
      //size_t x = *((size_t*) &cnt) >> 8;
      //byte* hash_copy = (byte*) (&x);
      for (long i = 0; i < std::min<long>(cnt, buffer_size); i++) 
        //if  (hashes[i] == hash)
          if (KeyEqual{}(buffer[i].key, key))
            return std::optional(buffer[i].value);
      // if (cnt > buffer_size && cnt <= 7) {
      //   bool found = false;
      //   for (long i = buffer_size; i < std::min<long>(cnt, 7); i++)
      //     found = found || (hash_copy[i] == hash);
      //   if (!found) return {};
      // }
      link* cur = next;
      while (cur != nullptr) {
        if (KeyEqual{}(cur->entry.key, key))
          return std::optional(cur->entry.value);
        cur = cur->next;
      }
      return {};
    }

    bool insert(const KV& kv, byte hash) { 
      if (find(kv.key, hash).has_value()) return false;
      if (cnt < buffer_size) {
        buffer[cnt] = kv;
        //hashes[cnt] = hash;
        cnt++;
      } else {
        if (cnt == 127) abort();
        // for (int i = std::min<int>(cnt,6); i > buffer_size; i--)
        //    hashes[i] = hashes[i-1];
        // hashes[buffer_size] = hash;
        cnt++;
        next = new_link(kv, next);
      }
      return true;
    }

    static std::pair<int, link*> remove_from_list(link* nxt, const K& k) {
      if (nxt == nullptr) return std::pair(-1, nullptr);
      else if (KeyEqual{}(nxt->entry.key, k)) {
        link* tmp = nxt->next;
        retire_link(nxt);
        return std::pair(0, tmp);
      } else {
        auto [num, ptr] = remove_from_list(nxt->next, k);
        if (num == -1) return std::pair(-1, ptr);
        return std::pair(num + 1, new_link(nxt->entry, ptr));
      }
    }

    bool remove(const K& key, byte hash) {
      for (long i = 0; i < std::min<long>(cnt, buffer_size); i++)
        if (KeyEqual{}(buffer[i].key, key)) {
          if (cnt <= buffer_size) {
            buffer[i] = buffer[cnt-1];
            //hashes[i] = hashes[cnt-1];
            cnt--;
          } else {
            buffer[i] = next->entry;
            link* tmp = next;
            next = next->next;
            retire_link(tmp);
            cnt--;
          }
          return true;
        }
      if (cnt <= buffer_size) return false;
      auto [idx, ptr] = remove_from_list(next, key);
      if (idx == -1) return false;
      // for (int i = buffer_size + idx; i < std::min<int>(cnt,6); i++)
      //   hashes[i] = hashes[i+1];
      // if (buffer_size + idx < 7 && cnt > 7) {
      //   link* l = ptr;
      //   while (l->next != nullptr) l = l->next;
      //   hashes[6] = get_hash(l->entry.key);
      // }
      next = ptr;
      cnt--;
      return true;
    }

    void retire() {
      link* nxt = next;
      while (nxt != nullptr) {
        link* tmp = nxt->next;
        retire_link(nxt);
        nxt = tmp;
      }
    }

    int size() {
      if (cnt <= buffer_size) return cnt;
      int len = buffer_size;
      link* nxt = next;
      while (nxt != nullptr) {
        len++;
        nxt = nxt->next;
      }
      return len;
    }

    template <typename F>
    void foreach(const F& f) {
      for (long i = 0; i < std::min<long>(cnt, buffer_size); i++)
        f(buffer[i]);
      link* cur = next;
      while (cur != nullptr) {
        f(cur->entry);
        cur = cur->next;
      }
    }
  };

  std::vector<state> table;

  unordered_map(size_t n) {
    long size = 2 * n / buffer_size;
    table = std::vector<state>(size);
  }

  ~unordered_map() {
    parlay::parallel_for (0, table.size(), [&] (size_t i) { table[i].retire(); });
  }

  std::pair<size_t, std::byte> get_bucket_and_hash(const K& key) {
    size_t h = Hash{}(key);
    return std::pair((h >> 8) % table.size(), (std::byte) (h & 255));}

  //static std::byte get_hash(const K& key) {
  //  return (std::byte) (Hash{}(key) & 255);}

  std::optional<V> find(const K& key) {
    auto [b, h] = get_bucket_and_hash(key);
    //volatile size_t x = ustm::load(*((size_t*) &table[b]));
    //if (state::check_not_found(x, h)) return {};
    state s = ustm::load(table[b]);
    return s.find(key, h);
  }

  std::optional<V> find_(const K& key) {return find(key);}
  std::optional<V> find_locked(const K& key) {return find(key);}
      
  bool insert(const K& key, const V& value) {
    auto [b, h] = get_bucket_and_hash(key);
    state s = ustm::load(table[b]);
    if (s.insert(KV{key, value}, h)) {
      ustm::store(table[b], s);
      return true;
    }
    return false;
  }

  bool remove(const K& key) {
    auto [b, h] = get_bucket_and_hash(key);
    state s = ustm::load(table[b]);
    if (s.remove(key, h)) {
      ustm::store(table[b], s);
      return true;
    }
    return false;
  }

  long size() {
    return parlay::reduce(parlay::tabulate(table.size(), [&] (size_t i) {
                                                           return table[i].size();}));
  }

  void print() {
    for (size_t i=0; i < table.size(); i++) 
      table[i].foreach([&] (KV& kv) {std::cout << kv.key << ", ";});
    std::cout << std::endl;
  }

  long check() { return size();}

  static void clear() { } // link_pool.clear(); }
  static void stats() { } // link_pool.stats(); }
  static void reserve(size_t n) {}
  static void shuffle(size_t n) {}

};

#endif // PARLAY_HASH_H_
