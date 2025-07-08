#ifndef USTM_COMMON_H_
#define USTM_COMMON_H_

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

    void insert_fits(const K k, const V v) {
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
      insert_fits(k,v);
    }
  
    void reset() {
      num_entries = 0;
      for (auto &a : entries) a.first = empty;
    }
  
    uhashMap() {}
  };

  // getStamp() returns the current time stamp
  // nextStamp() returns incremented stamp
  using TS = uint64_t; // type for timestamps
#define HW_STAMP
#ifdef HW_STAMP // use hardware timestamp (works with intel x86)
  struct timeStamp {
    TS getStamp() {
      unsigned int lo,hi;
      __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
      return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
    }
    TS nextStamp() { return getStamp(); }
  };
#else // use software timestamp 
  struct timeStamp {
    std::atomic<TS> ts = 1ul;
    TS getStamp() { return ts.load(); }
    TS nextStamp() {
      TS ts1 = ts.load(); // delay and try again to avoid fetch-and-increment
      for(volatile int i = 1; i < 400; i++) {}
      TS ts2 = ts.load();
      if (ts1 == ts2) return ++ts; // fetch and increment
      else return ts2;
    }
  };
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

  // Transaction descriptor, includes the logs, stats, and other state
  struct Descriptor {
    static constexpr int LogSize = 1ul << 12; // size of the logs
    std::jmp_buf env;
    bool readOnly {false};
    int inConstructor {0};
    uint64_t tid;
    uint64_t numRetries {0};    
    TS startStamp;
    TS epochStamp;

    // The following four entries are the logs.
    // Write log keeps address, a time stamp used during validation,
    // and a function that writes the value to a given location.
    std::vector<std::tuple<void*,std::function<void(void*)>,bool,TS>> writeLog;
    // Read log keeps pointer to locations.  Used to later validate reads.
    std::vector<const void*> readLog;
    // Alloc and delete logs keep pointers to functions that do deletes
    std::vector<std::function<void()>> allocLog;
    std::vector<std::function<void()>> deleteLog;

    // Hash table to find self writes
    uhashMap<const void*, int> writeTable;
    void reset() {
      writeLog.clear(); readLog.clear();
      deleteLog.clear(); allocLog.clear(); writeTable.reset();
      readOnly = false; inConstructor = false;
    }
  };

  // thread local copy of descriptor.  Should be null when no transaction.
  extern inline Descriptor*& get_descriptor() {
    static thread_local Descriptor* d = nullptr;
    return d;
  }
}

#endif // USTM_COMMON_H_
