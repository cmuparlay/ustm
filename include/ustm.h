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
#include <functional>
#include <iostream>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>
// Uses epoch-based SMR from uepoch.h.  Needed interface:
//   r = uepoch::protect(func) : runs func under protection
//   uepoch::delay(func) : delays until protected are done
//   uepoch::add_before_epoch_hook(func)
//   uepoch::thread_id()
#include "uepoch.h"

namespace ustm {
  // some constants
  constexpr int MaxThreads = 4096; // can be increased
  constexpr int logLocks = 22; // log_2 of number of locks
  static constexpr int filterBits = 10; // for hash filter

  using TS = size_t; // type for timestamps
  struct timeStamp {
    std::atomic<TS> ts = 1ul;
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
      return ++ts;
    }
  };
  
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
    T scratch;
    link(T* addr, T value) : version{addr}, value(value) {}
  };
  
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

    std::atomic<version*> verlist = nullptr; // version list
    version* getVersionList() const { return verlist.load();}
    
    // add to version list
    void addLink(version* l, TS prevStamp) {
      l->next = verlist.load();
      l->stamp = prevStamp;
      verlist.store(l, std::memory_order_release); }

    // If self locked return: <true, 0>
    // If succeedes in locking return: <true, prior timestamp>
    // If failed return: <false, 0>
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
    bool inTransaction {false};
    bool readOnly {false};
    bool lateRead {false};
    int inConstructor {0};
    size_t tid; // thread identifier
    size_t numRetries {0};    
    TS startStamp;
    STMState* stmState;
    
    // Entry for the write log.  Contains the location, an
    // oldStamp used when locked, a pointer to the link
    // containing the value, a pointer to the data within
    // the link and the size in bytes of the value.
    struct logEntry {
      void* loc;
      TS oldStamp;
      version* link;
      void* data;
      void* scratch;
      int size;
    };
    
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
    std::bitset<size_t{1} << filterBits> writeFilter;

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
        endStamp = ts.getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->startStamp,
                                       myd->tid))
            return false;
        // If here (i.e.,  locks and validates succeeded),
        // transaction succeeded.  Apply the writes and then
        // unlock them.
        for (auto& e : myd->writeLog) {
          auto *tmp = e.scratch;
          // swap destination and data (from the link)
          std::memcpy(tmp, e.loc, e.size);
          std::memcpy(e.loc, e.data, e.size);
          std::memcpy(e.data, tmp, e.size);
          // add link to version list
          TS writeStamp = ((e.oldStamp == 0)
                           ? endStamp : e.oldStamp);
          getLock(e.loc).addLink(e.link, writeStamp);
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
      if (hasWrite) ts.nextStamp(endStamp);
      return true;
    }

    inline void abortTransaction(Descriptor* myd) {
      // Release locks 
      for (auto& e : myd->writeLog) 
        if (e.oldStamp != 0) getLock(e.loc).unlock(e.oldStamp);
      // Apply deletes to allocated objects.
      for (auto& e : myd->allocLog) e();
      if (++myd->numRetries % 4000000 == 0) { 
        std::cout << "ustm: too many retries: "
                  << std::endl; abort();}
      myd->reset();
      ts.nextStamp(myd->startStamp);
    }
    
    STMState() {
      for (int i=0; i < MaxThreads; i++) {
        descriptors[i].tid = i;
        descriptors[i].stmState = this;
      }
      // ensures stamp is incremented whenever epoch is
      uepoch::add_before_epoch_hook([&] { ts.nextStamp(); });
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
        version* ptr = (myd->writeLog[(*a).second]).link;
        return (reinterpret_cast<link<T>*>(ptr))->value;
      }
    }

    version* nxt;
    bool regular = (myd->inTransaction &&
                    !myd->readOnly && !unprotected);
    if (regular) myd->readLog.push_back(&v);
    char tmp[sizeof(T)];
    T* location = reinterpret_cast<T*>(tmp);
    while (true) { 
      auto prevStatus = status;
      std::memcpy(tmp, &v, sizeof(T));
      nxt = lck.getVersionList();
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
    if (regular) myd->lateRead = true;
    TS stamp = lck.getStamp(status);
    // Chase down the version list to the right one
    while (stamp >= myd->startStamp && nxt != nullptr) {
      if (nxt->addr == &v)
        location = &(reinterpret_cast<link<T>*>(nxt)->value);
      stamp = nxt->stamp;
      nxt = nxt->next;
    }
    return *location;
  }

  // runs func in an transaction
  // func takes no argument and must return a value
  template<typename F>
  inline auto transaction(F&& func, bool readOnly = false) {
    Descriptor* myd = getDescriptor();
    STMState* stm = myd->stmState;
    // if nested then just run
    if (myd->inTransaction) return func(); 
    myd->numRetries = 0;
    decltype(func()) returnValue;
    myd->readOnly = readOnly;
    while (true) {
      myd->inTransaction = true;
      returnValue = uepoch::protect([&] {
        myd->startStamp = stm->ts.getStamp();
        return func();
      });
      myd->inTransaction = false;
      if (stm->commitTransaction(myd))
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
      T* ptr = new T(std::forward<Args>(args)...);
      myd->allocLog.push_back([=] {delete ptr;});
      myd->inConstructor--;
      return ptr;
    } else
      return new T(std::forward<Args>(args)...);
  }

  // adds to log and only applies deletes on success
  template<typename T>
  inline void Delete(T* obj) {
    if (obj == nullptr) return;
    Descriptor* myd = getDescriptor();
    if (!myd->inTransaction) delete obj;
    else myd->deleteLog.push_back([=] { delete obj;});
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
      // temporarily store value in new link
      auto* hp = ustm::New<link<T>>(&loc, v);
      ustm::Delete(hp); // delete is delayed
      myd->writeLog.push_back({
        .loc = &loc,
        .oldStamp = 0,
        .link = hp,
        .data = &hp->value,
        .scratch = &hp->scratch,
        .size = sizeof(T)
      });
    } 
  }

  template <typename T>
  inline void fence(T& loc) {
    ustm::transaction([=] {
       ustm::store(loc, ustm::load(loc));}); }
} // End namespace ustm

#endif // USTM_H_
