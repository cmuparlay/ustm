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
          version* newv;
          std::get<2>(e)(&newv);
          version* oldv = *((version**) std::get<0>(e));
          //version* newv = std::get<2>(e);
          if (oldv != nullptr && oldv->isLink) Delete((link*) oldv);
          version* val = newv;
          if (newv != nullptr && newv->stamp == 0) {
            newv->stamp = endStamp;
            newv->next = oldv;
          } else val = (version*) New<link>(newv, oldv, endStamp);
          *((version**) std::get<0>(e)) = val;
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

    template <typename T>
    T getval (T v) {
      return ((v != nullptr) && v->isLink) ? ((link*) v)->value : v; };
    
    template <typename T>
    inline T load(T& val, bool validate=true) {
      //inline version* load(version*& val, bool validate=true) {
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) {
        std::atomic_thread_fence(MO_acquire);
        return getval(val);
      }
      if (myd->readOnly) return load_snapshot(val, myd);
      // check if wrote it myself and read from write log if so
      auto x = myd->writeTable.find(&val);
      if (x.has_value()) {  // found write of location in write buffer
        version* r;
        std::get<2>(myd->writeLog[*x])((void*) &r);
        return r;
        // return std::get<2>(myd->writeLog[*x]);
      }
      lock& lck = getLock(&val);
      auto status = lck.loadStart();
      version* r = getval(val);
      std::atomic_thread_fence(std::memory_order_acquire);
      if (validate) {
        if (!lck.loadCheck(status, myd->startStamp))
          abortTransaction(myd);
        myd->readLog.push_back(&val); // to validate later
      } else if (!lck.loadUnprotectedCheck(status))
        abortTransaction(myd);
      return r;
    }

    inline version* load_snapshot(version*& val,
                                  Descriptor* myd) {
      while (getLock(&val).isLocked())
        for (volatile int i = 0; i < 100; i++);
      std::atomic_thread_fence(MO_acquire);
      version* nxt = val;
      while (nxt != nullptr && nxt->stamp > myd->startStamp)
        nxt = nxt->next;
      return ((nxt != nullptr) && nxt->isLink) ? ((link*) nxt)->value : nxt;
    }

    template <typename T>
    inline void store(T& loc, T v) {
      Descriptor* myd = get_descriptor();
      if (myd != nullptr) {
        int i = myd->writeLog.size();
        myd->writeLog.push_back(std::tuple(&loc, 0, [=] (void* p) {
          *((T*) p) = v;}));
        myd->writeTable.insert(&loc, i);
      } else loc = v;
    }

    template <typename T>
    inline void init(T& loc, T v) { loc = v; }

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
