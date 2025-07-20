// Supports transactions with the following interface:
//   r = ustm::transaction(func) // runs function (no args) in a transaction
//   r = ustm::transaction(func, true) // same but in read only mode
//   r = ustm::load(x) // loads value from x
//   r = ustm::load(x, true) // does an unprotected load on x
//   ustm::store(x, v) // stores v into x
//   r = ustm::New<T>(args ...) // generate new object of type T
//   ustm::Delete(x) // deletes x
// The types for load and store can be any relocatable type.
// Memory should only be allocated with the mystm version in a transaction.

#ifndef USTM_H_
#define USTM_H_

#include <atomic>
#include <bitset>
#include <cmath>
//#include <csetjmp>
#include <functional>
#include <iostream>
#include <thread>
#include <unordered_map>
#include <vector>
#include "uepoch.h"

#define MAX_THREADS 4096 // arbitrary, can be increased

namespace ustm {

  using TS = uint64_t; // type for timestamps
  struct timeStamp {
    std::atomic<TS> ts = 1ul;
    // returns current stamp
    TS getStamp() const { return ts.load(); }
    TS nextStamp() { // returns incremented stamp
      TS ts1 = ts.load(); // delay and try again to avoid fetch-and-increment
      for (volatile int i = 1; i < 500; i++) {}
      TS ts2 = ts.load();
      if (ts1 == ts2) return ++ts; // fetch and increment
      else return ts2;
    }
  };

  // Returns a unique thread id for each thread.
  // Increments numThreads first time it is called.
  extern inline int threadId() {
    static std::atomic<int> numThreads{0};
    static thread_local int id{numThreads.fetch_add(1)};
    return id;
  }

  // generic version link
  struct version {
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
    Status getStatus() const { return v.load(std::memory_order_acquire);}
    version* getVersionList() const { return verlist.load();}
    enum lckResult {Acquired, Self, Failed};
    
    // Returns whether acquired, self owned, or failed (i.e., locked
    // by another, or timestamp on lock is after startStamp).
    lckResult tryLock(int tid, const TS startStamp) {
      Status s = v.load(std::memory_order_acquire);
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
    void unlock(const TS ts) {
      v.store((ts == 0) ? oldStamp : ts, std::memory_order_release); }

    // add to version list
    void addLink(version* l) {
      l->next = verlist.load();
      l->stamp = oldStamp;
      verlist.store(l, std::memory_order_release); }

    // checks that either self locked or timestamp has not changed
    bool validateRead(const TS startStamp, int tid) const {
      Status s = v.load(std::memory_order_acquire);
      return ((isLocked(s) && getTID(s) == tid) ||
              isPrior(s, startStamp));  }
  };

  // Transaction descriptor, includes the logs, stats, and other state
  struct alignas(128) Descriptor {
    //std::jmp_buf continuation;
    bool readOnly {false};
    int inConstructor {0};
    uint64_t tid; // thread identifier
    uint64_t numRetries {0};    
    TS startStamp;

    // The following four entries are the logs.
    // Each entry of the write log contains the address, the pointer
    // to a link with the value, the size in bytes of the value, and a
    // flag used during validation.
    std::vector<std::tuple<void*,version*,void*,int,bool>> writeLog;
    // Each entry of the read log contains a pointer to a location.
    std::vector<const void*> readLog;

    // Alloc and delete logs keep pointers to functions that do deletes
    std::vector<std::function<void()>> allocLog;
    std::vector<std::function<void()>> deleteLog;

    // A hash map and filter to help reads find prior writes
    std::unordered_map<void*, int> writeMap;
    std::bitset<1024> writeFilter;
    
    void reset() {
      writeLog.clear(); readLog.clear();
      deleteLog.clear(); allocLog.clear();
      inConstructor = false;
      writeMap.clear(); writeFilter = 0;
    }
  };

  // thread local copy of descriptor.  Should be null when no transaction.
  extern inline Descriptor*& getDescriptor() {
    static thread_local Descriptor* d = nullptr;
    return d;
  }

  int hash(const void* addr) {
    return ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9); }

  struct STM {
    // state consists of locks, descriptors, and timestamp
    static constexpr uint64_t numLocks = 1ul << 18; // 256K locks
    Descriptor opDesc[MAX_THREADS];
    lock writeLocks[numLocks];
    timeStamp ts;

    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      return writeLocks[hash(addr) & (numLocks - 1)]; }

    inline void commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto& lck = getLock(std::get<0>(e));
          auto r = getLock(std::get<0>(e)).tryLock(myd->tid, myd->startStamp);
          if (r == lock::Failed) abortTransaction(myd);
          std::get<4>(e) = (r == lock::Acquired);
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
          auto [loc, ver, valloc, size, selfLocked] = e;
          char* tmp[size];
          // swap loc and data in lnk, and add link to version list
          std::memcpy(tmp, loc, size);
          std::memcpy(loc, valloc, size);
          std::memcpy(valloc, tmp, size);
          getLock(loc).addLink(ver);
          std::atomic_thread_fence(std::memory_order_release);
        }
        for (auto& e : myd->writeLog) // unlock writes
          if (std::get<4>(e))
            getLock(std::get<0>(e)).unlock(endStamp);
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
        if (std::get<4>(e)) getLock(std::get<0>(e)).unlock(0);
      for (auto& e : myd->allocLog)
        uepoch::delay(std::move(e));
      if (++myd->numRetries % 4000000 == 0) { 
        std::cout << "ustm: too many retries: " << std::endl; abort();}
      myd->reset();
      uepoch::get_epoch().unannounce();
      throw 0; //std::longjmp(myd->continuation, 1);
    }

    STM() { for (int i=0; i < MAX_THREADS; i++) opDesc[i].tid = i; }
    ~STM() {}
  };

  extern inline STM* getStmState() {
    static STM stm;
    return &stm;
  }

  template <typename T>
  inline T load(T& v, bool unprotected=false) {
    STM* stm = getStmState();
    Descriptor* myd = getDescriptor();
    lock& lck = stm->getLock(&v);
    auto status = lck.getStatus();

    // check if value is in the write buffer
    if (myd != nullptr && myd->writeFilter[hash(&v) & 1023ul]) {
      auto a = myd->writeMap.find((void*) &v);
      if (a != myd->writeMap.end()) 
        return ((link<T>*) std::get<1>(myd->writeLog[(*a).second]))->value;
    }
    char tmp[sizeof(T)];
    while (true) {
      auto olds = status;
      std::memcpy(tmp, &v, sizeof(T));
      std::atomic_thread_fence(std::memory_order_acquire);
      status = lck.getStatus();
      if (myd == nullptr || unprotected) {
        if (!lck.isLocked(olds) && olds == status)
          return *((T*) tmp);
      } else if (myd->readOnly) {
        if (!lck.isLocked(olds) && olds == status) {
          TS stamp = lck.getStamp(status);
          if (stamp < myd->startStamp) return *((T*) tmp);
          // Because of lazy stamps, we need to abort if on the first try
          // we see a timestamp equal to the startstamp.
          if (stamp == myd->startStamp && myd->numRetries == 0)
            stm->abortTransaction(myd);
          version* nxt = lck.getVersionList();
          version* found = nullptr;
          // Chase down the version list to the right one
          while (nxt != nullptr && nxt->stamp >= myd->startStamp ) {
            if (nxt->addr == &v) found = nxt;
            nxt = nxt->next;
          }
          if (nxt != nullptr && nxt->addr == &v) found = nxt;
          return (found == nullptr) ? *((T*) tmp) : ((link<T>*) found)->value;
        }
      } else {
        if (lck.isPrior(olds, myd->startStamp) && olds == status) {
          myd->readLog.push_back(&v); // to validate later
          return *((T*) tmp);
        } else stm->abortTransaction(myd); }
    }
  }


  // runs func in an transaction
  // func takes no argument and must return a value
  template<typename F>
  inline auto transaction(F&& func, bool readOnly = false) {
    STM* stm = getStmState();
    Descriptor*& d = getDescriptor();
    if (d != nullptr) return func(); // if nested then just run
    const int tid = threadId();
    Descriptor* myd = &(stm->opDesc[tid]);
    d = myd; // set the thread local (note d is a ref type)
    myd->numRetries = 0;
    myd->readOnly = readOnly;
    while (true) {
      try {
        // Using lazy timestamps from verlib.  Only increments
        // on second try avoiding too many increments.
        auto retval = uepoch::protect([&] {
                                        if (myd->numRetries == 0) myd->startStamp = stm->ts.getStamp();
                                        else if (readOnly) stm->ts.nextStamp();
                                        else myd->startStamp = stm->ts.nextStamp();
                                        return func();});
        stm->commitTransaction(myd);
        return retval;
      } catch (int x) {}
    }
  } 

  // adds to log so can delete if aborted
  template <typename T, typename... Args>
  inline T* New(Args&&... args) {
    Descriptor* myd = getDescriptor();
    if (myd != nullptr) {
      myd->inConstructor++;
      T* ptr = (T*) std::malloc(sizeof(T));
      myd->allocLog.push_back([=] { delete ptr; });
      new (ptr) T(args...);
      myd->inConstructor--;
      return ptr;
    } else {
      T* ptr = (T*) std::malloc(sizeof(T));
      new (ptr) T(args...);
      return ptr;
    }
  }

  // adds to log and only applies deletes on success
  template<typename T>
  inline void Delete(T* ptr) {
    if (ptr == nullptr) return;
    Descriptor* myd = getDescriptor();
    if (myd == nullptr) delete ptr;
    else myd->deleteLog.push_back([=] { delete ptr; });
  }

  template <typename T>
  inline void store(T& loc, const T& v) {
    Descriptor* myd = getDescriptor();
    if (myd != nullptr && myd->inConstructor == 0) {
      // temporarily store value in new link
      link<T>* hp = ustm::New<link<T>>(&loc, v);
      ustm::Delete(hp); // delete is delayed
      // add to write map so reads can find it
      myd->writeFilter[hash(&loc) & 1023ul] = 1;
      myd->writeMap[&loc] = myd->writeLog.size();
      myd->writeLog.push_back(std::tuple(&loc, (version*) hp, &hp->value,
                                         sizeof(T), false));
    } else loc = v;
  }
} // End namespace ustm

#endif // USTM_H_
