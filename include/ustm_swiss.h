// Supports transactions with the following interface:
//   r = ustm::transaction(func) // runs a func in transaction
//   r = ustm::transaction(func, true) // read only mode
//   r = ustm::load(x) // loads value from x
//   ustm::store(x, v) // stores v into x
//   r = ustm::New<T>(args ...) // new object of type T
//   ustm::Delete(x) // deletes x
// The types for load and store can be any relocatable type.

#ifndef USTM_H_
#define USTM_H_
#include <array>
#include <atomic>
#include <bitset>
#include <cmath>
#ifdef LongJmp
#include <csetjmp>
#endif
#include <functional>
#include <iostream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
// Uses epoch-based SMR from uepoch.h.  Needed interface:
//   r = uepoch::protect(func) : runs func under protection
//   uepoch::delay(func) : delays until proteced are done
//   uepoch::add_before_epoch_hook(func)
//   uepoch::thread_id()
#include "uepoch.h"
#ifdef ParlayAlloc
#include <parlay/alloc.h>
#endif
#if defined(Funnel) || defined(FunnelEager)
#ifdef SimpleFunnel
#ifdef USE_UEPOCH
#include "aggregating_funnel.h"
#else
#include "aggregating-funnels/counter/aggregatingFunnelCounter.hpp"
#endif
template<typename T>
using AggFunnel = SIMPLE_AGG_FUNNEL::AggFunnelCounter<T>;
#elif defined(FullFunnel)
#include "aggregating-funnels/counter/fullAggregatingFunnelCounter.hpp"
template<typename T>
using AggFunnel = FULL_AGG_FUNNEL::FullAggFunnelCounter<T>;
#elif defined(RecursiveFunnel)
#include "aggregating-funnels/counter/recursiveAggregatingFunnelCounter.hpp"
template<typename T>
using AggFunnel = RECURSIVE_AGG_FUNNEL::RecursiveAggFunnelCounter<T>;
#endif
#endif // Funnel

namespace ustm {
  // some constants
  constexpr int MaxThreads = 4096; // can be increased
  constexpr int logLocks = 22; // log_2 of number of locks
  static constexpr int filterBits = 10; // for hash filter

  using TS = size_t; // type for timestamps
#ifdef HWStamp
  struct timeStamp {
    TS getStamp() {
      unsigned int lo,hi;
      __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
      return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
    }
    TS nextStamp(TS prevStamp=0) {
      if (prevStamp == 0) prevStamp = getStamp();
      while (true) {
        TS stamp = getStamp();
        if (stamp > prevStamp) return stamp;
      }
    }
  };
#elif defined(Eager)
  struct timeStamp {
    std::atomic<TS> ts;
    TS getStamp() const { return ts.load(); }
    TS nextStamp(TS prevStamp=0) { return ts++; }
  };
#elif defined(FunnelEager)
  struct timeStamp {
    AggFunnel<TS> ts{0, uepoch::max_threads};
    TS getStamp() const { return ts.load(); }
    TS nextStamp(TS prevStamp=0) {
      return ts.fetch_add(1, uepoch::thread_id());
    }
  };
#else
#define SplitStamp
  struct timeStamp {
#ifdef Funnel
    AggFunnel<TS> ts{0, uepoch::max_threads};
#else
    std::atomic<TS> ts = 1ul;
#endif // AggFunnel
    // returns current stamp
    TS getStamp() const { return ts.load(); }
    // returns incremented stamp
    TS nextStamp(TS prevStamp=0) {
      if (prevStamp == 0) prevStamp = getStamp();
      TS stamp = ts.load(); 
      if (stamp > prevStamp) return stamp;
      for (volatile int i = 0; i < 200; i++);
      stamp = ts.load(); 
      if (stamp > prevStamp) return stamp;
#ifdef Funnel
      return ts.fetch_add(1, uepoch::thread_id());
#else
      return ++ts;
#endif // AggFunnel
    }
  };
#endif

#ifndef SimpleSTM
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
  #ifndef VLA
    T scratch;
  #endif // VLA
    link(T* addr, T value) : version{addr}, value(value) {}
  };
  
#endif
  // The lock structure.  Contains the status of the lock
  // and if using versioning a version list.
  struct alignas(16) lock {
    using Status = size_t;
    // If locked (high bit set) keeps tid of owner,
    // otherwise keeps timestamp
    std::atomic<Status> v = 1; // time starts at 1
    lock() {}
    static int getTID(Status s) { return ~(1ul << 63) & s;}
    static TS getStamp(Status s) { return s;}
    static bool isLocked(Status s) { return (s >> 63) & 1ul;}
    static Status setLock(int tid) {
      return Status((1ul << 63) | tid);}
    Status getStatus() const {
      return v.load(std::memory_order_acquire);}
    void setStamp(TS stamp) {
      v.store(stamp, std::memory_order_release); }
#ifndef SimpleSTM

    std::atomic<version*> verlist = nullptr; // version list
    version* getVersionList() const { return verlist.load();}
    
    // add to version list
    void addLink(version* l, TS prevStamp) {
      l->next = verlist.load();
      l->stamp = prevStamp;
      verlist.store(l, std::memory_order_release); }
#endif

    // Returns whether succeeded or not, and if succeeded a
    // 0 stamp.  If was self locked, and the swapped out
    // timestamp otherwise
    std::pair<bool,TS> tryLock(int tid, const TS startStamp) {
      Status s = getStatus();
      if (isLocked(s)) 
        if (getTID(s) == tid) return std::pair(true, 0);
        else return std::pair(false, 0);
      if (s < startStamp &&
          v.compare_exchange_strong(s, setLock(tid))) {
        return std::pair(true, getStamp(s));
      } else return std::pair(false, 0);
    }

    // unlocks by setting status to timestamp
    void unlock(const TS ts) {
      v.store(ts, std::memory_order_release); }

    // checks that either self locked or timestamp before
    // startStamp
    bool validateRead(const TS startStamp, int tid) const {
      Status s = getStatus();
      return ((isLocked(s) && getTID(s) == tid) ||
              s < startStamp); 
    }
  };

  struct STMState;
  
  // Transaction descriptor, includes the logs, stats, and
  // other state
  struct alignas(128) Descriptor {
#ifdef LongJmp
    std::jmp_buf continuation;
#endif
    bool inTransaction {false};
#ifndef SimpleSTM
    bool readOnly {false};
#endif
    bool lateRead {false};
    int inConstructor {0};
    size_t tid; // thread identifier
    size_t numRetries {0};    
    TS startStamp;
    STMState* stmState;
#ifdef STMStats
    long numAborts = 0;
    long numCommits = 0;
#endif
    
#ifdef SimpleSTM
    // Entry for the write log.  Contains the location, an
    // oldStamp used when locked, the value, and the size in
    // bytes of the value.
    struct logEntry { void* loc; TS oldStamp; size_t data; int size; };
#else
    // Entry for the write log.  Contains the location, an
    // oldStamp used when locked, a pointer to the link
    // containing the value, a pointer to the data within
    // the link and the size in bytes of the value.
    struct logEntry {
      void* loc;
      TS oldStamp;
      version* link;
      void* data;
  #ifndef VLA
      void* scratch;
  #endif // VLA
      int size;
    };
#endif
    
    // The following four members are the logs.
    std::vector<logEntry> writeLog;
    // The read log contains pointers to read locations.
    std::vector<const void*> readLog;

    // Alloc and delete logs keep pointers to functions that
    // do deletes
    std::vector<std::function<void()>> allocLog;
    std::vector<std::function<void()>> deleteLog;

    // A hash map and filter to let reads find prior writes
    std::unordered_map<void*, int> writeMap;
    std::bitset<filterBits> writeFilter;

    // used to reset between each attempt
    void reset() {
      if (writeLog.size() > 0) {
        writeFilter = 0; writeLog.clear(); writeMap.clear();
      }
      readLog.clear(); deleteLog.clear(); allocLog.clear();
      inConstructor = false; lateRead = false;
    }
  };

  template <int bits>
  size_t hash(const void* addr) {
    size_t x = ((reinterpret_cast<size_t>(addr)>>6) *
                UINT64_C(0xbf58476d1ce4e5b9));
    return x >> (64 - bits);
  }
  
  struct STMState {
    // state consists of locks, descriptors, and timestamp
    static constexpr size_t numLocks = 1ul << logLocks;
    std::array<Descriptor,MaxThreads> descriptors;
    std::array<lock,numLocks> writeLocks;
    timeStamp ts;

    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      return writeLocks[hash<logLocks>(addr)];} 

    // returns true if successful
    inline bool commitTransaction(Descriptor* myd) {
      TS endStamp;
      bool hasWrite = myd->writeLog.size() > 0;
      if (hasWrite) {
        if (myd->lateRead) return false;
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto [r,s] = getLock(e.loc).tryLock(myd->tid,
                                              myd->startStamp);
          if (!r) return false;
          e.oldStamp = s;
        }
        // get stamp -- this is the serialization point
#if defined(SplitStamp) || defined(LazyStamp)
        endStamp = ts.getStamp();
#else
        endStamp = ts.nextStamp();
#endif
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->startStamp,
                                       myd->tid))
            return false;
        // If  here (i.e.  locks  and validates  succeeded),
        // transaction succeeded.  Apply the writes and then
        // unlock them.
        for (auto& e : myd->writeLog) {
#ifdef SimpleSTM
          std::memcpy(e.loc, &e.data, e.size);
#else

  #ifdef VLA
          char tmp[e.size];
  #else
          auto *tmp = e.scratch;
  #endif // VLA
          // swap destination and data (from the link)
          std::memcpy(tmp, e.loc, e.size);
          std::memcpy(e.loc, e.data, e.size);
          std::memcpy(e.data, tmp, e.size);
          // add link to version list
          TS writeStamp = ((e.oldStamp == 0)
                           ? endStamp : e.oldStamp);
          getLock(e.loc).addLink(e.link, writeStamp);
#endif
        }
        std::atomic_thread_fence(std::memory_order_release);
        for (auto&  e : myd->writeLog) // unlock writes
         if (e.oldStamp != 0)
           getLock(e.loc).unlock(endStamp);
      }
      // Since successful, apply the deletes.
      for (auto& e : myd->deleteLog)
        uepoch::delay(std::move(e));
      myd->reset();
#ifdef SplitStamp
      if (hasWrite) ts.nextStamp(endStamp);
#endif
      myd->inTransaction = false;
#ifdef STMStats
      myd->numCommits++;
#endif
      return true;
    }

    inline void abortTransaction(Descriptor* myd) {
      // Release locks and apply deletes to allocated objects.
      for (auto& e : myd->writeLog) 
        if (e.oldStamp != 0) getLock(e.loc).unlock(e.oldStamp);
      for (auto& e : myd->allocLog)
        uepoch::delay(std::move(e));
      if (++myd->numRetries % 4000000 == 0) { 
        std::cout << "ustm: too many retries: "
                  << std::endl; abort();}
      myd->reset();
      ts.nextStamp(myd->startStamp);
#ifdef STMStats
      myd->numAborts++;
#endif
    }
    
    STMState() {
      for (int i=0; i < MaxThreads; i++) {
        descriptors[i].tid = i;
        descriptors[i].stmState = this;
      }
#ifndef SimpleSTM
      // ensures stamp is incremented whenever epoch is
      uepoch::add_before_epoch_hook([&] { ts.nextStamp(); });
#endif
    }
    ~STMState() {}
  };
      
  extern inline Descriptor* initDescriptor() {
    static STMState stm;
    return &stm.descriptors[uepoch::thread_id()];
  }

  // thread local copy of descriptor.
  extern inline Descriptor* getDescriptor() {
    static thread_local Descriptor* d = initDescriptor();
    return d;
  }

  template <typename T>
  inline T load(T& v, bool unprotected=false) {
    //__builtin_prefetch (&v);
    Descriptor* myd = getDescriptor();
    lock& lck = myd->stmState->getLock(&v);
    auto status = lck.getStatus();

    // check if value is in the write buffer
    if (myd->writeLog.size() > 0 &&
        myd->writeFilter[hash<filterBits>(&v)]) {
      auto a = myd->writeMap.find((void*) &v);
      if (a != myd->writeMap.end()) {
#ifdef SimpleSTM
        auto& e = myd->writeLog[(*a).second];
        std::remove_const_t<T> r;
        std::memcpy(&r, &e.data, e.size);
        return r;
#else
        version* ptr = (myd->writeLog[(*a).second]).link;
        return (reinterpret_cast<link<T>*>(ptr))->value;
#endif
      }
    }

#ifdef SimpleSTM
    bool regular = (myd->inTransaction && !unprotected);
#else
    version* nxt;
    bool regular = (myd->inTransaction &&
                    !myd->readOnly && !unprotected);
#endif
    if (regular) myd->readLog.push_back(&v);
    char tmp[sizeof(T)];
    T* location = reinterpret_cast<T*>(tmp);
    while (true) { 
      auto prevStatus = status;
      std::memcpy(tmp, &v, sizeof(T));
#ifndef SimpleSTM
      nxt = lck.getVersionList();
#endif
      std::atomic_thread_fence(std::memory_order_acquire);
      status = lck.getStatus();
      if (prevStatus == status) {
        if (status < myd->startStamp) return *location;
        if (!lock::isLocked(status)) {
          if (!myd->inTransaction) return *location;
          break;
        }
      }
    }
#if defined(LongJmp)
    if (regular) {
      myd->lateRead = true;
      uepoch::get_epoch().unannounce();
      std::longjmp(myd->continuation, 1);
    }
#elif defined(TryThrow)
    if (regular) {
      uepoch::get_epoch().unannounce();
      throw 0;
    }
#else
    if (regular) myd->lateRead = true;
#endif
#ifdef SimpleSTM
    return *location;
#else
    TS stamp = lck.getStamp(status);
    // Chase down the version list to the right one
    while (stamp >= myd->startStamp && nxt != nullptr) {
#ifdef LazyStamp
      if (status == myd->startStamp) {
        myd->lateRead = true;
#if defined(LongJmp)
        uepoch::get_epoch().unannounce();
        std::longjmp(myd->continuation, 1);
#elif defined(TryThrow)
        uepoch::get_epoch().unannounce();
        throw 0;
#endif
      }
#endif
      if (nxt->addr == &v)
        location = &(reinterpret_cast<link<T>*>(nxt)->value);
      stamp = nxt->stamp;
      nxt = nxt->next;
    }
    return *location;
#endif
  }

  // runs func in an transaction
  // func takes no argument and must return a value
  template<typename F>
  inline auto transaction(F&& func, bool readOnly = false) {
    Descriptor* myd = getDescriptor();
    STMState* stm = myd->stmState;
    // if nested then just run
    if (myd->inTransaction) return func(); 
    myd->inTransaction = true;
    myd->numRetries = 0;
    decltype(func()) returnValue;
#ifndef SimpleSTM
    myd->readOnly = readOnly;
#endif
    while (true) {
#ifdef LongJmp
      if (!setjmp(myd->continuation)) {
#elif defined(TryThrow)
      try {
#endif
      returnValue = uepoch::protect([&] {
        myd->startStamp = stm->ts.getStamp();
        return func();
      });
#ifdef LongJmp
      }
      if (!myd->lateRead && stm->commitTransaction(myd)) 
#elif defined(TryThrow)
      } catch (int x) {}
      if (!myd->lateRead && stm->commitTransaction(myd))
#elif defined(LazyStamp)
        if ((!myd->lateRead ||
             (myd->readOnly && myd->numRetries > 0))
            && stm->commitTransaction(myd))
#else
      if (stm->commitTransaction(myd))
#endif
        return returnValue;
      else stm->abortTransaction(myd);
    }
  } 

  // adds to log so can delete if aborted
  template <typename T, typename... Args>
  inline T* New(Args&&... args) {
    Descriptor* myd = getDescriptor();
    if (myd->inTransaction) {
      myd->inConstructor++;
#ifdef ParlayAlloc
      T* ptr = parlay::type_allocator<T>::create(args...);
      myd->allocLog.push_back([=] {
         parlay::type_allocator<T>::destroy(ptr);});
#else
      T* ptr = new T(args...);
      myd->allocLog.push_back([=] {delete ptr;});
#endif
      myd->inConstructor--;
      return ptr;
    } else
#ifdef ParlayAlloc
      return parlay::type_allocator<T>::create(args...);
#else
      return new T(args...);
#endif
  }

  // adds to log and only applies deletes on success
  template<typename T>
  inline void Delete(T* obj) {
    if (obj == nullptr) return;
    Descriptor* myd = getDescriptor();
#ifdef ParlayAlloc
    if (!myd->inTransaction)
      parlay::type_allocator<T>::destroy(obj);
    else myd->deleteLog.push_back([=] {
      parlay::type_allocator<T>::destroy(obj);});
#else
    if (!myd->inTransaction) delete obj;
    else myd->deleteLog.push_back([=] { delete obj;});
#endif
  }

  template <typename T>
  inline void store(T& loc, const T& v) {
    Descriptor* myd = getDescriptor();
    if (myd->inConstructor > 0) loc = v;
    else if (!myd->inTransaction)
      transaction([&] {ustm::store(loc, v); return true;});
    else {
      // insert into hash filter and map so reads can find it
      myd->writeFilter[hash<filterBits>(&loc)] = 1;
      myd->writeMap[&loc] = myd->writeLog.size();
#ifdef SimpleSTM
      Descriptor::logEntry e{&loc, 0, 0, sizeof(T)};
      // store value in data field (at most 8 bytes)
      std::memcpy(&e.data, &v, sizeof(T));
      myd->writeLog.push_back(e);
#else
      // temporarily store value in new link
      auto* hp = ustm::New<link<T>>(&loc, v);
      ustm::Delete(hp); // delete is delayed
      myd->writeLog.push_back({
        .loc = &loc,
        .oldStamp = 0,
        .link = hp,
        .data = &hp->value,
  #ifndef VLA
        .scratch = &hp->scratch,
  #endif // VLA
        .size = sizeof(T)
      });
#endif
    } 
  }

  template <typename T>
  inline void fence(T& loc) {
    ustm::transaction([=] {
       ustm::store(loc, ustm::load(loc));}); }
#ifdef STMStats

  std::pair<long,long> statistics() {
    STMState* stm = getDescriptor()->stmState;
    uint64_t totalAborts = 0;
    uint64_t totalCommits = 0;
    for (int i=0; i < uepoch::num_threads(); i++) {
      auto& d = stm->descriptors[i];
      totalAborts += d.numAborts;
      totalCommits += d.numCommits;
      d.numAborts = d.numCommits = 0;
    }
    return std::pair(totalAborts, totalCommits);
  }
#endif
} // End namespace ustm

#endif // USTM_H_
