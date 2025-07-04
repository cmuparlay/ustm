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

namespace ustm {

#define MAX_THREADS 256

  // Transaction descriptor, includes the logs, stats, and other state
  struct Descriptor {
    static constexpr int LogSize = 1ul << 12; // size of the logs
    std::jmp_buf env;
    uint64_t tid;
    uint64_t numAborts {0};
    uint64_t numCommits {0};
    uint64_t numRetries {0};    
    TS startStamp;

    // The following four entries are the logs.
    // Write log keeps address, a time stamp used during validation,
    // and a function that writes the value to a given location.
    std::vector<std::tuple<void*,TS,std::function<void(void*)>>> writeLog;
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
        for (auto& e : myd->writeLog) 
          std::get<2>(e)(std::get<0>(e));
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
    inline T load(T& val, bool validate=true) {
      Descriptor* myd = get_descriptor();
      if (myd == nullptr) { std::atomic_thread_fence(MO_acquire); return val; }
      // check if wrote it myself and read from write log if so
      auto x = myd->writeTable.find(&val);
      if (x.has_value()) { // found write of location in write buffer
        typename std::remove_const<T>::type r;
        // apply write into r
        std::get<2>(myd->writeLog[*x])((void*) &r);
        return r;
      }
      // Reads timestamp (status) before copying data to a local
      // buffer (tmp) and then again after.  Only succeeds if equal
      // and less than start stamp.
      lock& lck = getLock(&val);
      auto status = lck.loadStart();
      char tmp[sizeof(T)];
      std::memcpy(tmp, &val, sizeof(T));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (validate) {
        if (!lck.loadCheck(status, myd->startStamp))
          abortTransaction(myd);
        myd->readLog.push_back(&val); // to validate later
      } else if (!lck.loadUnprotectedCheck(status))
        abortTransaction(myd);
      return *((T*) tmp);
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

    // adds to log so can delete if aborted
    template <typename T, typename... Args>
    static T* New(Args&&... args) {
      Descriptor* myd = get_descriptor();
      //get_descriptor() = nullptr;
      T* ptr = new T(args...);
      //get_descriptor() = myd;
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

#endif // USTM_H_
