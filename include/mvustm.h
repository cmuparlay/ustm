// Supports transactions with the following interface
//
//   STM mystm;  // declare a transaction structure
//   r = mystm.transaction(func) // runs function (no args) in a transaction
//   r = mystm.transaction(func, true) // same but in read only mode
//   r = mystm.load(x) // loads value from x
//   r = mystm.load(x, true) // does an unprotected load on x
//   mystm.store(x, v) // stores v into x
//   r = mystm.New<T>(args ...) // generate new object of type T
//   mystm.Delete(x) // deletes x
//
// The types for load and store can be any relocatable type.
// Memory should only be allocated with the mystm version in a transaction.

#ifndef USTM_H_
#define USTM_H_

#include <atomic>
#include <cmath>
#include <csetjmp>
#include <functional>
#include <iostream>
#include <thread>
#include <vector>
#include "uepoch.h"

#define MAX_THREADS 256
#define MO_acquire std::memory_order_acquire
#define MO_release std::memory_order_release

namespace ustm {

  // growable hashmap used for searching the write log
  template <typename K, typename V>
  struct hashMap {
  private:
    using KV = std::pair<K, V>;
    static constexpr K empty = (K) 0ul;
    int logOfSize = 3; // start size = 2^3 = 8
    std::vector<KV> entries;
    size_t mask = 0;
    long numEntries = 0;
    size_t hash(K const& k) const noexcept {
      return (uint64_t)(k) * UINT64_C(0xbf58476d1ce4e5b9);  }

    // insert without growing
    void insertNoGrow(const K k, const V v) {
      int loc = hash(k) & mask;
      while (entries[loc].first != empty && entries[loc].first != k)
        loc = (loc + 1) & mask;
      entries[loc] = KV(k,v);
      numEntries++;
    }

  public:
    std::optional<V> find(const K k) const {
      int loc = hash(k) & mask;
      if (entries.size() == 0) return {};
      while (entries[loc].first != empty && entries[loc].first != k)
        loc = (loc + 1) & mask;
      if (entries[loc].first == k) return entries[loc].second;
      else return {};
    }

    void insert(const K k, const V v) {
      // if too many entries, then double the size and copy into new table
      if (numEntries >= entries.size()/2) {
        logOfSize++;
        std::vector<KV> newEntries(1ul << logOfSize, KV(empty,V{}));
        mask = newEntries.size() - 1;
        swap(entries, newEntries);
        for (auto kv : newEntries)
          if (kv.first != empty) insertNoGrow(kv.first,kv.second);
      }
      insertNoGrow(k,v);
    }
  
    void reset() { // clear the table
      numEntries = 0;
      for (auto &a : entries) a.first = empty;
    }
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
    TS getStamp() const { return ts.load(); }
    TS nextStamp() {
      TS ts1 = ts.load(); // delay and try again to avoid fetch-and-increment
      for (volatile int i = 1; i < 500; i++) {}
      TS ts2 = ts.load();
      if (ts1 == ts2) return ++ts; // fetch and increment
      else return ts2;
    }
  };
#endif

  // keep track of number of active threads
  extern inline std::atomic<int>& numThreads() {
    static std::atomic<int> numThreads;
    return numThreads;
  }

  // returns a unique thread id for each thread
  // increments numThreads first time it is called
  extern inline int threadId() {
    static thread_local int id{numThreads().fetch_add(1)};
    return id;
  }

  // generic version link
  struct  version {
    version* next = nullptr;
    TS stamp = 0;
    void* addr;
    version(void* addr) : addr(addr) {}
  };

  // link including a type specific value
  template <typename T>
  struct link : version {
    T value;
    link(T* addr, T value) : version{addr}, value(value) {}
  };

  struct alignas(32) lock {
    using Status = uint64_t;
    // If locked (high bit set) keeps tid of owner, otherwise keeps timestamp
    std::atomic<Status> v = 1; 
    std::atomic<version*> verlist = nullptr; // the version list
    TS oldStamp; // used to temporarily stash old stamp when locked
    lock() {}
    static int getTID(Status s) { return ~(1ul << 63) & s;}
    static TS getStamp(Status s) { return s;}
    static bool isLocked(Status s) { return (s >> 63) & 1ul;}
    static bool isPrior(Status s, const TS timeStamp) {return s < timeStamp;}
    static Status setLock(int tid) {return Status((1ul << 63) | tid);}
    Status getStatus() const { return v.load(MO_acquire);}
    version* getVersionList() const { return verlist.load();}
    enum lckResult {Acquired, Self, Failed};
    
    // Returns whether acquired, self owned, or failed (i.e., locked
    // by another, or timestamp on lock is after startStamp).
    lckResult tryLock(int tid, const TS startStamp) {
      Status s = v.load(MO_acquire);
      if (isLocked(s))
        if (getTID(s) == tid) return Self;
        else return Failed;
      if (isPrior(s, startStamp) &&
          v.compare_exchange_strong(s, setLock(tid))) {
        oldStamp = getStamp(s);
        return Acquired;
      } else return Failed;
    }

    // unlocks by setting status to timestamp
    // timeStamp == 0 indicates it aborted, so restore stamp
    void unlock(const TS timeStamp, int tid) {
      v.store((timeStamp == 0) ? oldStamp : timeStamp, MO_release); }

    // add to version list
    void addLink(version* l) {
      l->next = verlist.load();
      l->stamp = oldStamp;
      verlist.store(l, MO_release); }

    // checks that either self locked or timestamp has not changed
    bool validateRead(const TS startStamp, int tid) const {
      Status s = v.load(MO_acquire);
      return ((isLocked(s) && getTID(s) == tid) ||
              isPrior(s, startStamp));
    }
  };

  // Transaction descriptor, includes the logs, stats, and other state
  struct alignas(128) Descriptor {
    std::jmp_buf continuation;
    bool readOnly {false};
    int inConstructor {0};
    uint64_t tid; // thread identifier
    uint64_t numRetries {0};    
    TS startStamp;

    // The following four entries are the logs.
    // Each entry of the write log contains the address, the pointer
    // to a link with the value, the size in bytes of the value, and a
    // flag used during validation.
    std::vector<std::tuple<void*,version*,int,bool>> writeLog;                                        
    // Each entry of the read log contains a pointer to a location.
    std::vector<const void*> readLog;
    // Alloc and delete logs keep pointers to functions that do deletes
    std::vector<std::function<void()>> allocLog;
    std::vector<std::function<void()>> deleteLog;

    // A hash map to help reads find prior writes
    hashMap<const void*, int> writeTable;

    void reset() {
      writeLog.clear(); readLog.clear();
      deleteLog.clear(); allocLog.clear(); writeTable.reset();
      inConstructor = false;
    }
  };

  // thread local copy of descriptor.  Should be null when no transaction.
  extern inline Descriptor*& getDescriptor() {
    static thread_local Descriptor* d = nullptr;
    return d;
  }

  class STM {
  private:
    // state consists of locks, descriptors, and timestamp
    static constexpr uint64_t numLocks = 1ul << 18; // 256K locks
    lock writeLocks[numLocks];
    Descriptor opDesc[MAX_THREADS];
    timeStamp ts;
    
    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      uint64_t hash = ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9);
      return writeLocks[hash & (numLocks - 1)]; }

    inline void commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto& lck = getLock(std::get<0>(e));
          auto r = getLock(std::get<0>(e)).tryLock(myd->tid, myd->startStamp);
          if (r == lock::Failed) abortTransaction(myd);
          std::get<3>(e) = (r == lock::Acquired);
        }
        // get stamp -- this is the serialization point
        TS endStamp = ts.getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->startStamp, myd->tid))
            abortTransaction(myd);
        // If here (i.e. locks and validates succeeded), transaction
        // succeeded.  Apply the writes and then unlock them.
        for (auto& e : myd->writeLog) {
          auto [loc, ver, size, selfLocked] = e;
          char* tmp[size];
          // swap loc and data in lnk
          char* newVal = &(((link<char>*) ver)->value);
          std::memcpy(tmp, loc, size);
          std::memcpy(loc, newVal, size);
          std::memcpy(newVal, tmp, size);
          getLock(loc).addLink(ver);
        }
        // fence required since writes are not individually fenced
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& e : myd->writeLog)
          if (std::get<3>(e))
            getLock(std::get<0>(e)).unlock(endStamp, myd->tid);
      }
      // Since successful, apply the deletes.
      for (auto& e : myd->deleteLog)
        uepoch::delay(std::move(e));
      myd->reset();
      getDescriptor() = nullptr;
    }

    inline void abortTransaction(Descriptor* myd) {
      // Release locks and apply deletes to allocated objects.
      for (auto& e : myd->writeLog) 
        if (std::get<3>(e)) getLock(std::get<0>(e)).unlock(0, myd->tid);
      for (auto& e : myd->allocLog)
        uepoch::delay(std::move(e));
      if (++myd->numRetries % 4000000 == 0) { 
        std::cout << "ustm: too many retries: " << std::endl; abort();}
      myd->reset();
      uepoch::get_epoch().unannounce();
      std::longjmp(myd->continuation, 1);
    }

    // Like a sequence lock.  Keeps trying until the status is not
    // locked and does not change so bytes are properly copied.
    template <typename T>
    inline T safeLoad(const T& val, const lock& lck, lock::Status& s) {
      char tmp[sizeof(T)];
      while (true) {
        auto olds = s;
        std::memcpy(tmp, &val, sizeof(T));
        std::atomic_thread_fence(MO_acquire);
        s = lck.getStatus();
        if (!lck.isLocked(s) && olds == s)
          return *((T*) tmp);
      }
    }

    // for read only transactions
    template <typename T>
    inline T loadSnapshot(const T& val, Descriptor* myd) {
      auto& lck = getLock(&val);
      lock::Status status;
      T r = safeLoad(val, lck, status);
      TS stamp = lck.getStamp(status);
      if (stamp < myd->startStamp) return r;
      // Because of lazy stamps, we need to abort if on the first try
      // we see a timestamp equal to the startstamp.
      if (stamp == myd->startStamp && myd->numRetries == 0)
        abortTransaction(myd);
      version* nxt = lck.getVersionList();
      version* found = nullptr;
      // Chase down the version list to the right one
      while (stamp >= myd->startStamp && nxt != nullptr) {
        if (nxt->addr == &val) found = nxt;
        stamp = nxt->stamp;
        nxt = nxt->next;
      }
      return (found == nullptr) ? r : ((link<T>*) found)->value;
    }

  public:
    STM() { for (int i=0; i < MAX_THREADS; i++) opDesc[i].tid = i; }
    ~STM() {} 

    template <typename T>
    inline T load(const T& v, bool unprotected=false) {
      Descriptor* myd = getDescriptor();
      lock& lck = getLock(&v);
      auto status = lck.getStatus();
      if (myd == nullptr) return safeLoad(v, lck, status);
      if (myd->readOnly) return loadSnapshot(v, myd);

      // check if in write buffer
      auto x = myd->writeTable.find(&v);
      if (x.has_value())
        return ((link<T>*) std::get<1>(myd->writeLog[*x]))->value;

      if (unprotected) return safeLoad(v, lck, status);
      char tmp[sizeof(T)];
      std::memcpy(tmp, &v, sizeof(T));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (!lck.isPrior(status, myd->startStamp) ||
          lck.getStatus() != status)
        abortTransaction(myd);
      myd->readLog.push_back(&v); // to validate later
      return *((T*) tmp);
    }

    // runs func in an transaction
    // func takes no argument and must return a value
    template<typename F>
    auto transaction(F&& func, bool readOnly = false) {
      Descriptor*& d = getDescriptor();
      if (d != nullptr) return func(); // if nested then just run
      const int tid = threadId();
      Descriptor* myd = &opDesc[tid];
      d = myd; // set the thread local (note d is a ref type)
      myd->numRetries = 0;
      myd->readOnly = readOnly;
      setjmp(myd->continuation); // return to here if aborted
      // Using lazy timestamps from verlib.  Only increments
      // on second try avoiding too many increments.
      if (myd->numRetries == 0) myd->startStamp = ts.getStamp();
      else if (readOnly) ts.nextStamp();
      else myd->startStamp = ts.nextStamp();
      auto retval = uepoch::protect([&] {return func();});
      commitTransaction(myd);
      return retval;
    } 

    // adds to log so can delete if aborted
    template <typename T, typename... Args>
    static T* New(Args&&... args) {
      Descriptor* myd = getDescriptor();
      if (myd != nullptr) {
        myd->inConstructor++;
        T* ptr = new T(args...);
        myd->inConstructor--;
        myd->allocLog.push_back([=] {delete ptr;});
        return ptr;
      } else return new T(args...);
    }

    // adds to log and only applies deletes on success
    template<typename T>
    static void Delete(T* obj) {
      if (obj == nullptr) return;
      Descriptor* myd = getDescriptor();
      if (myd == nullptr) delete obj;
      else myd->deleteLog.push_back([=] {delete obj;});
    }

    template <typename T>
    inline void store(T& loc, const T& v) {
      Descriptor* myd = getDescriptor();
      if (myd != nullptr && myd->inConstructor == 0) {
        link<T>* hp = New<link<T>>(&loc, v);
        Delete(hp);
        myd->writeTable.insert(&loc, myd->writeLog.size());
        myd->writeLog.push_back(std::tuple(&loc, (version*) hp,
                                           sizeof(T), false));
      } else loc = v;
    }
  };
} // End namespace ustm

#endif // USTM_H_
