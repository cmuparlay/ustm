#pragma once
#include "flock/flock.h"
#include "timestamps.h"

namespace verlib {
  const TS tbd = std::numeric_limits<TS>::max()/4;

  
  template <typename T>
  using atomic = flck::atomic<T>;
  using lock = flck::lock;
  using atomic_bool = flck::atomic_write_once<bool>;
  using flck::memory_pool;
  template <typename F>
  auto do_now(F f) {return f();}

struct versioned {};

struct version_link { // Called Node in pseudo-code
  TS lower_time_stamp;
  TS upper_time_stamp;
  version_link* next_version;
  void* value;
  version_link() : lower_time_stamp(0), upper_time_stamp(0) {}
  version_link(TS lower_time, TS upper_time, version_link* next, void* value) :
    lower_time_stamp(lower_time), upper_time_stamp(upper_time), next_version(next), value(value) {}  
};

flck::memory_pool<version_link> link_pool;

// versioned_ptr<Node> left;
// Node* node = left;


// struct Node {
//   int a;
//   std::vector<int> b;
//   Node(int aa, int bb) : a(aa), b(bb) {}
// };

template <typename V>
struct versioned_ptr {  // Called Component in pseudo-code
private:
  std::atomic<version_link*> verlist;
  std::atomic<V*> value;
  
public:

  versioned_ptr(): verlist(link_pool.New(0, 0, nullptr, nullptr)), value(nullptr) {}
  versioned_ptr(V* ptr) : verlist(link_pool.New(0, 0, nullptr, nullptr)), value(ptr) {}

  ~versioned_ptr() { link_pool.Delete(verlist.load()); }
  void init(V* ptr) { store(ptr); }
  
  bool forward() {
    version_link* head = verlist.load();
    TS ts = global_stamp.get_stamp();  // ts = ver from pseudo-code
    if(ts <= head->upper_time_stamp) return true;
    // abort();
    version_link* node = link_pool.New(head->upper_time_stamp, ts, head, value.load());
    if(verlist.compare_exchange_strong(head, node)) {
        link_pool.Retire(head);   
        return true;
    }
    link_pool.Delete(node);
    return false;
  }

  V* read_snapshot() { // called readArchive()
    version_link* head = verlist.load();
    if(local_stamp >= head->upper_time_stamp) forward();
    head = verlist.load();
    if(local_stamp >= head->upper_time_stamp) forward();
    head = verlist.load();
    while(head->lower_time_stamp > local_stamp)
      head = head->next_version;
    return (V*) head->value;
  }

  V* load() {  // can be used anywhere
    if (local_stamp != -1) return read_snapshot();
    else {
      if(!forward()) forward();
      return value.load();
    }
  }

  // // only safe on journey
  // V* read() {  return (V*) v.read()->value; }

  void validate() { }

  void store(V* ptr) {
    if(!forward()) forward();   
    value.store(ptr);  
  }

  bool cas(V* old_v, V* new_v) {
    if(!forward()) forward();
    return value.compare_exchange_strong(old_v, new_v);  
  }

  V* operator=(V* b) {store(b); return b; }
};

  template <typename T, typename E>
  bool validate(flck::lock& lck, T* v, E expected) {
    return true;
  }

} // namespace verlib
