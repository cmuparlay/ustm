#ifndef USTM_UTILS_H_
#define USTM_UTILS_H_

#include <atomic>
#include <cmath>
#include <csetjmp>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>

#define MO_acquire std::memory_order_acquire
#define MO_release std::memory_order_release

namespace ustm {
  
  template <typename K, typename V>
  struct uhashMap {
    using KV = std::pair<K, V>;
    static constexpr K empty = (K) 0ul;
    int bits = 5;
    std::vector<KV> entries;
    size_t mask = 0;
    long num_entries = 0;
    size_t hash(K const& k) const noexcept {
      auto x =  (uint64_t)(k) * UINT64_C(0xbf58476d1ce4e5b9); 
      return (x ^ (x >> 31)); }  // linear then non-linear transform
  
    std::optional<V> find(const K k) {
      int loc = hash(k) & mask;
      if (entries.size() == 0) return {};
      while (entries[loc].first != empty && entries[loc].first != k)
        loc = (loc + 1) & mask;
      if (entries[loc].first == k) return entries[loc].second;
      else return {};
    }

    void insert_(const K k, const V v) {
      int loc = hash(k) & mask;
      while (entries[loc].first != empty && entries[loc].first != k)
        loc = (loc + 1) & mask;
      entries[loc] = KV(k,v);
      num_entries++;
    }
  
    void insert(const K k, const V v) {
      if (num_entries >= entries.size()/2) {
        bits = bits + 1;
        std::vector<KV> new_entries(1ul << bits, KV(empty,V{}));
        mask = new_entries.size() - 1;
        swap(entries, new_entries);
        for (auto kv : new_entries)
          if (kv.first != empty) insert_(kv.first,kv.second);
      }
      insert_(k,v);
    }
  
    void reset() {
      num_entries = 0;
      for (auto &a : entries) a.first = empty;
    }
  
    uhashMap() {}
  };

  // getStamp() returns the current timd stamp
  // nextStamp() returns the current time stamp and ensures
  //   it is incremented beyond that stamp before returning
  using TS = uint64_t; // type for timestamps
#define HW_STAMP
#ifdef HW_STAMP // use hardware timestamp (works with intel x86)
  TS getStamp() {
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
  }
  TS nextStamp() {
    return getStamp(); }
#else // use software timestamp 
  extern inline std::atomic<TS>& timeStamp() {
    static std::atomic<TS> ts;
    return ts; }
  TS getStamp() { return timeStamp().load(); }
  TS nextStamp() {
    TS ts1 = getStamp(); // delay and try again to avoid fetch-and-add
    for(volatile int i = 1; i < 400; i++) {}
    TS ts2 = getStamp();
    if (ts1 == ts2) return timeStamp()++;
    else return ts2-1;
  }
#endif
  
  // keep track of number of active threads
  extern inline std::atomic<int>& num_threads() {
    static std::atomic<int> num_threads;
    return num_threads;
  }

  // returns a unique thread id for each thread
  // increments num_threads first time it is called
  extern inline int thread_id() {
    static thread_local int id{num_threads().fetch_add(1)};
    return id;
  }

  // If locked (high bit set) keeps tid of owner, otherwise keeps timestamp
  struct lock {
    using Status = uint64_t;
    std::atomic<Status> v = 1;
    lock() {}
    static int getTID(Status s) { return ~(1ul << 63) & s;}
    static TS getStamp(Status s) { return s;}
    static bool isLocked(Status s) { return (s >> 63) & 1ul;}
    static bool isPrior(Status s, TS timeStamp) {return s < timeStamp;}
    static Status setLock(int tid) {return Status((1ul << 63) | tid);}
    bool isLocked() { return isLocked(v.load());}
    
    // Returns 0 if self locked, timestamp if succeeded, or {} if try
    // lock failed (i.e., is either locked by another or timestamp on
    // lock is after startStamp).
    std::optional<TS> tryLock(int tid, TS startStamp) {
      Status s = v.load(MO_acquire);
      if (isLocked(s))
        if (getTID(s) == tid) return 0;
        else return {};
      if (isPrior(s, startStamp) &&
          v.compare_exchange_strong(s, setLock(tid)))
        return getStamp(s);
      return {};
    }

    // unlocks by setting status to timestamp
    void unlock(TS timeStamp) { v.store(timeStamp, MO_release); }

    // checks that either self locked or timestamp has not changed
    bool validateRead(int tid, TS startStamp) {
      Status s = v.load(MO_acquire);
      return ((isLocked(s) && getTID(s) == tid) ||
              isPrior(s, startStamp));
    }

    // Wrap the following two functions around a load to check if it
    // was safe (i.e., no updates overlapped the load).
    Status loadStart() { return v.load(MO_acquire);}
    bool loadCheck(Status s, TS startStamp) {
      return (isPrior(s, startStamp) && v.load(MO_acquire) == s);}
    bool loadUnprotectedCheck(Status& s) {
      return (!isLocked(s) && v.load(MO_acquire) == s);}
  };
}

#endif // USTM_UTILS_H_
