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

#ifndef USTM_H_
#define USTM_H_

#include <atomic>
#include <thread>
#include <iostream>
#include <functional>
#include <csetjmp>
#include "uepoch.h"
#include "common.h"

#define MAX_THREADS 256

namespace ustm {
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
    timeStamp ts;

    // Returns a lock corresponding to a hash of the address
    inline lock& getLock(const void* addr) {
      uint64_t hash = ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9);
      return writeLocks[hash & (numLocks - 1)]; }

    void multiVersionWrite(version** loc, std::function<void(void*)>& write_f,
                           TS endStamp) {
      version* newv;
      write_f(&newv);
      version* oldv = *loc;
      if (oldv != nullptr && oldv->isLink) Delete((link*) oldv);
      version* val = newv;
      if (newv != nullptr && newv->stamp == 0) {
        newv->stamp = endStamp;
        newv->next = oldv;
      } else val = (version*) New<link>(newv, oldv, endStamp);
      *loc = val;
    }

    inline void commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto r = getLock(std::get<0>(e)).tryLock(myd->tid, myd->startStamp);
          if (!r.has_value()) abortTransaction(myd);
          std::get<3>(e) = *r; // stash timestamp to restore if aborted
        }
        TS endStamp = ts.getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!getLock(e).validateRead(myd->tid, myd->startStamp))
            abortTransaction(myd);
        // If here (i.e. locks and validates succeeded), transaction
        // succeeded.  Apply the writes and then unlock them.
        for (auto& e : myd->writeLog) {
          auto [loc, write_f, is_mv, ignore] = e;
          if (!is_mv) write_f(loc);
          else multiVersionWrite((version**) loc, write_f, endStamp);
        }
        // fence required since writes are not individually fenced
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& e : myd->writeLog) 
          if (std::get<3>(e) != 0) 
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
        if (std::get<3>(e) != 0) 
          getLock(std::get<0>(e)).unlock(std::get<3>(e)); // restore stamp
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
    inline T load(T& v, bool validate=true, bool mv=false) {
      using T_ = std::remove_const_t<T>;
      auto getval = [&] () -> T {
        if constexpr (!std::is_same_v<T_,version*>) return v;
        else return ((v != nullptr) && v->isLink) ? ((link*) v)->value : v; };
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) {
        std::atomic_thread_fence(MO_acquire);
        return getval();
      }

      // if versioned and read only, then load snapshot
      if constexpr (std::is_same_v<T_,version*>)
        if (myd->readOnly) return load_snapshot(v, myd);
      
      // check if wrote it myself and read from write log if so
      auto x = myd->writeTable.find(&v);
      if (x.has_value()) { // found write of location in write buffer
        typename std::remove_const<T>::type r;
        // apply write into r
        std::get<1>(myd->writeLog[*x])((void*) &r);
        return r;
      }

      // Reads timestamp (status) before copying data to a local
      // buffer (tmp) and then again after.  Only succeeds if equal
      // and less than start stamp.
      lock& lck = getLock(&v);
      auto status = lck.loadStart();
      char tmp[sizeof(T)];
      if constexpr (!std::is_same_v<T,version*>)
        std::memcpy(tmp, &v, sizeof(T));
      else *((T_*) tmp) = getval();
      std::atomic_thread_fence(std::memory_order_acquire);
      if (validate) {
        if (!lck.loadCheck(status, myd->startStamp))
          abortTransaction(myd);
        myd->readLog.push_back(&v); // to validate later
      } else if (!lck.loadUnprotectedCheck(status))
        abortTransaction(myd);
      return *((T*) tmp);
    }

    inline version* load_snapshot(version*& val, Descriptor* myd) {
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
      if (myd != nullptr && myd->inConstructor == 0) {
        bool is_mv = std::is_same_v<T,version*>;
        int i = myd->writeLog.size();
        myd->writeLog.push_back(std::tuple(&loc, [=] (void* p) {
                     *((T*) p) = v;}, is_mv, 0));
        myd->writeTable.insert(&loc, i);
      } else loc = v;
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
          myd->startStamp = (myd->numRetries > 0) ? ts.nextStamp() : ts.getStamp();
          auto retval = func();
          commitTransaction(myd);
          return retval;});
      } else { // func does not have result
        uepoch::with_epoch([&] { 
          setjmp(myd->env); // return to here if aborted
          myd->startStamp = (myd->numRetries > 0) ? ts.nextStamp() : ts.getStamp();
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
          myd->startStamp = ts.nextStamp()-1;
          auto retval = func();
          myd->reset();
          d = nullptr;
          return retval;});
    }

    // adds to log so can delete if aborted
    template <typename T, typename... Args>
    static T* New(Args&&... args) {
      Descriptor* myd = get_descriptor();
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
    void Delete(T* obj) {
      if (obj == nullptr) return;
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) delete obj;
      else myd->deleteLog.push_back([=] {delete obj;});
    }
  };
} // End namespace ustm

#endif // USTM_H_
