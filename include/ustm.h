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
#include "uhashMap.h"

#define MO_acquire std::memory_order_acquire
#define MO_release std::memory_order_release

namespace ustm {

#define MAX_THREADS 256

  // getStamp() returns the current timd stamp
  // nextStamp() returns the current time stamp and ensures
  //   it is incremented beyond that stamp before returning
  using TS = uint64_t; // type for timestamps
#define HW_STAMP
#ifdef HW_STAMP // use hardware timestamp (works with intel x86)
  TS getStamp() {
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
  }
  TS nextStamp() {
    return getStamp(); }
#else // use software timestamp 
  extern inline std::atomic<TS>& timeStamp() {
    static std::atomic<TS> ts;
    return ts; }
  TS getStamp() { return timeStamp().load(); }
  TS nextStamp() {
    TS ts1 = getStamp(); // delay and try again to avoid fetch-and-add
    for(volatile int i = 1; i < 400; i++) {}
    TS ts2 = getStamp();
    if (ts1 == ts2) return timeStamp()++;
    else return ts2-1;
  }
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

  // A set of shared locks.   Addresses are hashed to one of the locks.
  struct timedLocks {
  public:
    // If locked (high bit set) keeps tid of owner, otherwise keeps timestamp
    struct Status {
      uint64_t v;
      int getTID() { return ~(1ul << 63) & v;}
      TS getStamp() { return v;}
      bool isLocked() { return (v >> 63) & 1ul;}
      bool isPrior(TS timeStamp) {return v < timeStamp;}
      static Status setLock(int tid) {return Status((1ul << 63) | tid);}
      Status() noexcept = default;
      Status(TS stamp) : v(stamp) {}
    };
    static constexpr uint64_t numLocks = 1ul << 18; // 256K locks
    std::atomic<Status> writeLocks[numLocks];
  public:
    timedLocks() {
      for (int i = 0; i < numLocks; i++)
        writeLocks[i].store(Status(1));}

    // Returns a lock id in [0 .. numLocks) based on hash of the address
    uint64_t getLockNum(const void* addr) {
      uint64_t hash = ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9);
      return hash & (numLocks - 1); }
    
    // Returns 0 if self locked, timestamp if succeeded, or {} if try
    // lock failed (i.e., is either locked by another or timestamp on
    // lock is after startStamp).
    std::optional<TS> tryLock(const void* addr, int tid, TS startStamp) {
      uint64_t lockNum = getLockNum(addr);
      auto status = writeLocks[lockNum].load(MO_acquire);
      if (status.isLocked())
        if (status.getTID() == tid) return 0;
        else return {};
      if (status.isPrior(startStamp) &&
          writeLocks[lockNum].compare_exchange_strong(status,
                                                      Status::setLock(tid))) 
        return status.getStamp();
      return {};
    }

    // unlocks by setting status to timestamp
    void unlock(const void* addr, TS timeStamp) {
      writeLocks[getLockNum(addr)].store(Status(timeStamp), MO_release); };

    // checks that either self locked or timestamp has not changed
    bool validateRead(const void* addr, int tid, TS startStamp) {
      auto status = writeLocks[getLockNum(addr)].load(MO_acquire);
      return ((status.isLocked() && status.getTID() == tid) ||
              status.isPrior(startStamp));
    }

    // Wrap the following two functions around a load to check if it
    // was safe (i.e., no updates overlapped the load).
    Status loadStart(uint64_t idx) {
      return writeLocks[idx].load(MO_acquire);}
    bool loadCheck(uint64_t idx, Status& status, TS startStamp) {
      return (status.isPrior(startStamp) &&
              writeLocks[idx].load(MO_acquire).getStamp()==status.getStamp());}
  };

  class STM {
  private:
    timedLocks locks;
    Descriptor opDesc[MAX_THREADS];

    inline void commitTransaction(Descriptor* myd) {
      if (myd->writeLog.size() > 0) {
        // Take locks (abort if any fail)
        for (auto& e : myd->writeLog) {
          auto r = locks.tryLock(std::get<0>(e), myd->tid, myd->startStamp);
          if (!r.has_value()) abortTransaction(myd);
          std::get<1>(e) = *r; // stash timestamp to restore if aborted
        }
        TS endStamp = getStamp();
        // Validate the reads (abort if any fail)
        for (auto& e : myd->readLog) 
          if (!locks.validateRead(e, myd->tid, myd->startStamp))
            abortTransaction(myd);
        // If here (i.e. locks and validates succeeded), transaction
        // succeeded.  Apply the writes and then unlock them.
        for (auto& e : myd->writeLog) 
          std::get<2>(e)(std::get<0>(e));
        // fence required since writes are not individually fenced
        std::atomic_thread_fence(std::memory_order_release);
        for (auto& e : myd->writeLog) 
          if (std::get<1>(e) != 0)
            locks.unlock(std::get<0>(e), endStamp);
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
          locks.unlock(std::get<0>(e), std::get<1>(e)); // restore stamp
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
      uint64_t idx = locks.getLockNum(&val);
      auto status = locks.loadStart(idx);
      // copy data into a buffer (tmp), and then loadCheck
      char tmp[sizeof(T)];
      std::memcpy(tmp, &val, sizeof(T));
      std::atomic_thread_fence(std::memory_order_acquire);
      if (!locks.loadCheck(idx, status, myd->startStamp))
        abortTransaction(myd);
      if (validate) myd->readLog.push_back(&val); // to validate later
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

#endif // USTM_H_
