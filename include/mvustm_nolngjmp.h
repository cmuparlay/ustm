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
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#include <functional>
#include <iostream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
#include "uepoch.h"
#ifdef LongJmp
#include <csetjmp>
#endif

constexpr int MaxThreads=4096; // arbitrary, can be increased

namespace ustm {
  using TS = uint64_t; // type for timestamps

#ifdef HWStamp
  struct timeStamp {
    TS getStamp() {
      unsigned int lo,hi;
      __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
      return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
    }
    TS nextStamp() { return getStamp(); }
  };
#else
  struct timeStamp {
    std::atomic<TS> ts = 1ul;
    // returns current stamp
    TS getStamp() const { return ts.load(); }
    TS nextStamp() { // returns incremented stamp
      TS stamp1 = ts.load(); // delay to avoid fetch-and-increment
      for (volatile int i = 1; i < 500; i++) {}
      TS stamp2 = ts.load();
      if (stamp1 == stamp2) return ++ts; // fetch and increment
      else return stamp2;
    }
  };
#endif

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
    TS previousStamp = 0; // stamp of previous update
    lock() {}
    static int getTID(Status s) { return ~(1ul << 63) & s;}
    static TS getStamp(Status s) { return s;}
    static bool isLocked(Status s) { return (s >> 63) & 1ul;}
    static bool isPrior(Status s, const TS timeStamp) {return s < timeStamp;}
    static Status setLock(int tid) {return Status((1ul << 63) | tid);}
    Status getStatus() const { return v.load(std::memory_order_acquire);}
    version* getVersionList() const { return verlist.load();}
    void setStamp(TS stamp) { v.store(stamp, std::memory_order_release); }
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
        previousStamp = getStamp(s);
        return Acquired;
      } else return Failed;
    }

    // unlocks by setting status to timestamp
    // timeStamp == 0 indicates it aborted, so restore stamp
    void unlock(const TS ts) {
      v.store((ts == 0) ? previousStamp : ts, std::memory_order_release); }

    // add to version list
    void addLink(version* l, TS endStamp) {
      l->next = verlist.load();
      l->stamp = previousStamp;
      previousStamp = endStamp;
      verlist.store(l, std::memory_order_release); }

    // checks that either self locked or timestamp has not changed
    bool validateRead(const TS startStamp, int tid) const {
      Status s = v.load(std::memory_order_acquire);
      return ((isLocked(s) && getTID(s) == tid) ||
              isPrior(s, startStamp));  }
  };

  static constexpr int filterBits = 10;

  // Transaction descriptor, includes the logs, stats, and other state
  struct alignas(128) Descriptor {
#ifdef LongJmp
    std::jmp_buf continuation;
#endif
    bool readOnly {false};
    bool toAbort {false};
    bool large {false};
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
    std::bitset<filterBits> writeFilter;
    
    void reset() {
      writeLog.clear(); readLog.clear();
      deleteLog.clear(); allocLog.clear();
      inConstructor = false; toAbort = false;
      writeMap.clear(); writeFilter = 0;
    }
  };

  // thread local copy of descriptor.  Should be null when no transaction.
  extern inline Descriptor*& getDescriptor() {
    static thread_local Descriptor* d = nullptr;
    return d;
  }

  template <int bits>
  size_t hash(const void* addr) {
    size_t x = (reinterpret_cast<size_t>(addr)>>6) * UINT64_C(0xbf58476d1ce4e5b9);
    return x >> (64 - bits);
  }
  
  struct STM {
    // state consists of locks, descriptors, and timestamp
    static constexpr int logLocks = 19;
    static constexpr uint64_t numLocks = 1ul << logLocks;
    std::array<Descriptor,MaxThreads> opDesc;
    std::array<lock,numLocks> writeLocks;
    timeStamp ts;

    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      return writeLocks[hash<logLocks>(addr)];} 

    // returns true if successful
    inline bool commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto& lck = getLock(std::get<0>(e));
          auto r = getLock(std::get<0>(e)).tryLock(myd->tid, myd->startStamp);
          if (r == lock::Failed) return false;
          std::get<4>(e) = (r == lock::Acquired);
        }
        // get stamp -- this is the serialization point
        TS endStamp = ts.getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->startStamp, myd->tid))
            return false;
        // If here (i.e. locks and validates succeeded), transaction
        // succeeded.  Apply the writes and then unlock them.
        for (auto& e : myd->writeLog) {
          auto [destLocation, ver, dataLocation, size, selfLocked] = e;
          char* tmp[size];
          // swap destination and data (from the link)
          std::memcpy(tmp, destLocation, size);
          std::memcpy(destLocation, dataLocation, size);
          std::memcpy(dataLocation, tmp, size);
          // add link to version list
          getLock(destLocation).addLink(ver, endStamp);
        }
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& e : myd->writeLog) // unlock writes
          if (std::get<4>(e))
            getLock(std::get<0>(e)).unlock(endStamp);
      }
      // Since successful, apply the deletes.
      for (auto& e : myd->deleteLog)
        uepoch::delay(std::move(e));
      myd->reset();
      getDescriptor() = nullptr;
      return true;
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
#ifdef LongJmp
      uepoch::get_epoch().unannounce();
      std::longjmp(myd->continuation, 1);
#elif defined(TryThrow)
      uepoch::get_epoch().unannounce();
      throw 0;
#endif      
    }

    STM() {
      for (int i=0; i < MaxThreads; i++) opDesc[i].tid = i;
      uepoch::add_before_epoch_hook([&] { ts.nextStamp(); });
    }
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
    if (myd != nullptr && myd->writeFilter[hash<filterBits>(&v)]) {
      auto a = myd->writeMap.find((void*) &v);
      if (a != myd->writeMap.end()) {
        version* ptr = std::get<1>(myd->writeLog[(*a).second]);
        return (reinterpret_cast<link<T>*>(ptr))->value;
      }
    }

    version* nxt;
    char tmp[sizeof(T)];
    while (true) { 
      auto olds = status;
      std::memcpy(tmp, &v, sizeof(T));
      nxt = lck.getVersionList();
      std::atomic_thread_fence(std::memory_order_acquire);
      status = lck.getStatus();
      if (!lck.isLocked(olds) && olds == status) break;
      for (volatile int i=0; i < 200; i++);
    }
    if (myd == nullptr || unprotected) return *((T*) tmp);
    if (!myd->readOnly) myd->readLog.push_back(&v); // to validate later
    TS stamp = lck.getStamp(status);
    if (stamp < myd->startStamp) return *((T*) tmp);
    if (!myd->readOnly || // always abort if updating
        // For read we need to abort if on the first try
        // we see a timestamp equal to the startstamp.
        (!myd->large && stamp == myd->startStamp && myd->numRetries == 0)) {
#if defined(LongJmp) || defined(TryThrow)
      stm->abortTransaction(myd);
#else
      myd->toAbort = true;      
#endif
    }
    T* location = (T*) tmp;
    // Chase down the version list to the right one
    while (stamp >= myd->startStamp && nxt != nullptr) {
      if (nxt->addr == &v) location = &((link<T>*) nxt)->value;
      stamp = nxt->stamp;
      nxt = nxt->next;
    }
    return *location;
  }

  // runs func in an transaction
  // func takes no argument and must return a value
  template<typename F>
  inline auto transaction(F&& func, bool readOnly = false, bool large = false) {
    STM* stm = getStmState();
    Descriptor*& d = getDescriptor();
    if (d != nullptr) return func(); // if nested then just run
    const int tid = threadId();
    Descriptor* myd = &(stm->opDesc[tid]);
    d = myd; // set the thread local (note d is a ref type)
    myd->numRetries = 0;
    myd->readOnly = readOnly;
    myd->large = large;
#ifdef LongJmp
    setjmp(myd->continuation); // return to here if aborted
#elif defined(TryThrow)
    while (true) {
      try {
#else
    while (true) {
#endif
    // Using lazy timestamps from verlib.  Only increments
    // on second try avoiding too many increments.
      auto retval = uepoch::protect([&] {
        if (large) myd->startStamp = stm->ts.nextStamp();
        else if (myd->numRetries == 0) myd->startStamp = stm->ts.getStamp();
        else if (readOnly) stm->ts.nextStamp();
        else myd->startStamp = stm->ts.nextStamp();
        return func();});
#ifdef LongJmp
      if (!stm->commitTransaction(myd))
        stm->abortTransaction(myd);
      return retval;
#elif defined(TryThrow)
      if (!stm->commitTransaction(myd))
        stm->abortTransaction(myd);
      return retval;
    } catch (int x) {}
  }
#else
      if (!myd->toAbort && stm->commitTransaction(myd))
        return retval;
      stm->abortTransaction(myd);
    }
#endif
  } 

  // adds to log so can delete if aborted
  template <typename T, typename... Args>
  inline T* New(Args&&... args) {
    Descriptor* myd = getDescriptor();
    if (myd != nullptr) {
      myd->inConstructor++;
#ifdef LongJmp
      T* ptr = (T*) std::malloc(sizeof(T));
      myd->allocLog.push_back([=] {delete ptr;});
      new (ptr) T(args...);
#else
      T* ptr = new T(args...);
      myd->allocLog.push_back([=] {delete ptr;});
#endif      
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
  inline void Delete(T* obj) {
    if (obj == nullptr) return;
    Descriptor* myd = getDescriptor();
    if (myd == nullptr) delete obj;
    else myd->deleteLog.push_back([=] { delete obj;});
  }

  template <typename T>
  inline void store(T& loc, const T& v) {
    Descriptor* myd = getDescriptor();
    if (myd != nullptr && myd->inConstructor == 0) {
      // temporarily store value in new link
      link<T>* hp = ustm::New<link<T>>(&loc, v);
      ustm::Delete(hp); // delete is delayed
      // add to write map (and filter) so reads can find it
      myd->writeFilter[hash<filterBits>(&loc)] = 1;
      myd->writeMap[&loc] = myd->writeLog.size();
      myd->writeLog.push_back(std::tuple(&loc, hp, &hp->value,
                                         sizeof(T), false));
    } else loc = v;
  }
} // End namespace ustm

#endif // USTM_H_
