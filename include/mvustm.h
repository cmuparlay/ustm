// Supports transactions with the following interface
//
//   STM mystm;  // declare a transaction structure
//   r = mystm.transaction(func) // runs function (no args) in a transaction
//   r = mystm.load(x) // does a protected load on x
//   mystm.store(x, v) // stores v into x
//   r = mystm.New<T>(args ...) // generate new object of type T
//   mystm.Delete(x) // deletes x
//   std::pair<long,long> mystm.statistics() // total # of aborts and commits
//
// The types for load and store can be any relocatable type.
// Memory should only be allocated with the mystm version in a transaction.

#ifndef MVUSTM_H_
#define MVUSTM_H_

#include <atomic>
#include <thread>
#include <iostream>
#include <functional>
#include <csetjmp>
#include "uepoch.h"
#include "common.h"

namespace ustm {

#define MAX_THREADS 256

  struct version {
    version* next = nullptr;
    TS stamp = 0;
    bool isLink = false;
  };

  struct link : version {
    version* value;
    link(version* value, version* next, TS stamp)
      : version{next, stamp, true}, value(value) {}
  };

  // Transaction descriptor, includes the logs, stats, and other state
  struct Descriptor {
    static constexpr int LogSize = 1ul << 12; // size of the logs
    std::jmp_buf env;
    bool readOnly {false};
    uint64_t tid;
    uint64_t numAborts {0};
    uint64_t numCommits {0};
    uint64_t numRetries {0};    
    TS startStamp;

    // The following four entries are the logs.
    // Write log keeps address, a time stamp used during validation,
    // and a function that writes the value to a given location.
    std::vector<std::tuple<std::atomic<version*>*,TS,version*>> writeLog;
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
      readOnly = false;
    }
  };

  // thread local copy of descriptor.  Should be null when no transaction.
  extern inline Descriptor*& get_descriptor() {
    static thread_local Descriptor* d = nullptr;
    return d;
  }

  class STM {
  private:
    // state consists of locks and descriptors
    static constexpr uint64_t numLocks = 1ul << 18; // 256K locks
    lock writeLocks[numLocks];
    Descriptor opDesc[MAX_THREADS];

    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      uint64_t hash = ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9);
      return writeLocks[hash & (numLocks - 1)]; }

    inline void commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto r = getLock(std::get<0>(e)).tryLock(myd->tid, myd->startStamp);
          if (!r.has_value()) abortTransaction(myd);
          std::get<1>(e) = *r; // stash timestamp to restore if aborted
        }
        TS endStamp = getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->tid, myd->startStamp))
            abortTransaction(myd);
        // If here (i.e. locks and validates succeeded), transaction
        // succeeded.  Apply the writes and then unlock them.
        for (auto& e : myd->writeLog) {
          version* oldv = std::get<0>(e)->load();
          version* newv = std::get<2>(e);
          if (oldv != nullptr && oldv->isLink) Delete((link*) oldv);
          version* val = newv;
          if (newv != nullptr && newv->stamp == 0) {
            newv->stamp = endStamp;
            newv->next = oldv;
          } else val = (version*) New<link>(newv, oldv, endStamp);
          std::get<0>(e)->store(val);
         }
        // fence required since writes are not individually fenced
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& e : myd->writeLog) 
          if (std::get<1>(e) != 0)
            getLock(std::get<0>(e)).unlock(endStamp);
      }
      // Since successful, apply the deletes.
      for (auto& e : myd->deleteLog)
        uepoch::delay(std::move(e));
      myd->numCommits++;
      myd->reset();
      get_descriptor() = nullptr;
    }

    inline void abortTransaction(Descriptor* myd) {
      // Release locks and apply deletes to allocated objects.
      for (auto& e : myd->writeLog) 
        if (std::get<1>(e) != 0) 
          getLock(std::get<0>(e)).unlock(std::get<1>(e)); // restore stamp
      for (auto& e : myd->allocLog)
        uepoch::delay(std::move(e));
      myd->numAborts++;
      if (++myd->numRetries % 4000000 == 0) { 
        std::cout << "ustm: too many retries" << std::endl; abort();}
      myd->reset();
      std::longjmp(myd->env, 1);
    }

  public:
    STM() { for (int i=0; i < MAX_THREADS; i++) opDesc[i].tid = i; }
    ~STM() {} 

    std::pair<long,long> statistics() {
      uint64_t totalAborts = 0;
      uint64_t totalCommits = 0;
      for (int i=0; i < num_threads().load(); i++) {
        totalAborts += opDesc[i].numAborts;
        totalCommits += opDesc[i].numCommits;
      }
      return std::pair(totalAborts,totalCommits);
    }

    inline version* load(const std::atomic<version*>& val, bool validate=true) {
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) {
        version* v = val.load();
        return ((v != nullptr) && v->isLink) ? ((link*) v)->value : v;
      }
      if (myd->readOnly) return load_snapshot(val, myd);
      // check if wrote it myself and read from write log if so
      auto x = myd->writeTable.find(&val);
      if (x.has_value())  // found write of location in write buffer
        return std::get<2>(myd->writeLog[*x]);
      lock& lck = getLock(&val);
      auto status = lck.loadStart();
      version* v = val.load();
      version* r = ((v != nullptr) && v->isLink) ? ((link*) v)->value : v;
      if (validate) {
        if (!lck.loadCheck(status, myd->startStamp))
          abortTransaction(myd);
        myd->readLog.push_back(&val); // to validate later
      } else if (!lck.loadUnprotectedCheck(status))
        abortTransaction(myd);
      return r;
    }

    inline version* load_snapshot(const std::atomic<version*>& val,
                                  Descriptor* myd) {
      while (getLock(&val).isLocked())
        for (volatile int i = 0; i < 100; i++);
      version* nxt = val.load();
      while (nxt != nullptr && nxt->stamp > myd->startStamp)
        nxt = nxt->next;
      return ((nxt != nullptr) && nxt->isLink) ? ((link*) nxt)->value : nxt;
    }

    inline void init(std::atomic<version*>& loc, version* v) {
      loc.store(v);
    }
          
    inline void store(std::atomic<version*>& loc, version* v) {
      Descriptor* myd = get_descriptor();
      if (myd != nullptr) {
        int i = myd->writeLog.size();
        myd->writeLog.push_back(std::tuple(&loc, 0, v));
        myd->writeTable.insert(&loc, i);
      } else {std::cout << "ouch" << std::endl; loc = v;}
    }

    // func can either return a value or not
    template<typename F>
    auto transaction(F&& func) {
      Descriptor*& d = get_descriptor();
      if (d != nullptr) return func(); // if nested then just run
      const int tid = thread_id();
      Descriptor* myd = &opDesc[tid];
      d = myd; // set the thread local (note d is a ref type)
      myd->numRetries = 0;
      if constexpr (!std::is_void_v<std::invoke_result_t<F>>) { 
        return uepoch::with_epoch([&] { // func has result
          setjmp(myd->env); // return to here if aborted
          myd->startStamp = (myd->numRetries > 0) ? nextStamp() : getStamp();
          auto retval = func();
          commitTransaction(myd);
          return retval;});
      } else { // func does not have result
        uepoch::with_epoch([&] { 
          setjmp(myd->env); // return to here if aborted
          myd->startStamp = (myd->numRetries > 0) ? nextStamp() : getStamp();
          func();
          commitTransaction(myd);
          return;});
      }
    }

    // func must return a value
    template<typename F>
    auto transactionReadOnly(F&& func) {
      Descriptor*& d = get_descriptor();
      const int tid = thread_id();
      Descriptor* myd = &opDesc[tid];
      d = myd; // set the thread local (note d is a ref type)
      myd->readOnly = true;
      return uepoch::with_epoch([&] { // func has result
          myd->startStamp = nextStamp();
          auto retval = func();
          myd->reset();
          d = nullptr;
          return retval;});
    }

    // adds to log so can delete if aborted
    template <typename T, typename... Args>
    static T* New(Args&&... args) {
      T* ptr = new T(args...);
      Descriptor* myd = get_descriptor();
      if (myd != nullptr) myd->allocLog.push_back([=] {delete ptr;});
      return ptr;
    }

    // adds to log and only applies deletes on success
    template<typename T>
    void Delete(T* obj) {
      if (obj == nullptr) return;
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) delete obj;
      else myd->deleteLog.push_back([=] {delete obj;});
    }
  };
} // End namespace ustm

#endif // MVUSTM_H_
