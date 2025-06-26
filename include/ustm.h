// Supports transactions with the following interface
//
//   STM mystm;  // declare a transaction structure
//   r = mystm.transaction(func) // runs function (no args) in a transaction
//   r = mystm.load(x) // does a protected load on x
//   mystm.store(x, v) // stores v into x
//   r = mystm.New<T>(args ...) // generate new object of type T
//   mystm.Delete(x) // deletes x
//   std::pair<long,log> mystm.statistics() // total # of aborts and commits
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
#include <string>
#include <bitset>
#include "uepoch.h"

#define MO_acquire std::memory_order_acquire
#define MO_release std::memory_order_release

namespace ustm {

#define MAX_THREADS 256

  using TS = uint64_t;
  std::atomic<long> stamp = 0;

#define HW_STAMP
  // getStamp() returns the current timd stamp
  // nextStamp() returns the current time stamp and ensures
  //   it is incremented beyond that stamp before returning
#ifdef HW_STAMP
  TS getStamp() {
    unsigned int lo,hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return (TS) (((uint64_t)(hi & ~(1<<31)) << 32) | lo);
  }
  TS nextStamp() {
    for(volatile int i = 1; i < 400; i++) {}
    return getStamp(); }
#else
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

  // ignores lowest 5 bits of address
  uint64_t hashAddress(const void* addr) {
    auto x =  ((uint64_t)(addr) >> 5) * UINT64_C(0xbf58476d1ce4e5b9); 
    return (x ^ (x >> 31)); }  // linear then non-linear transform
  
  // Used for read, write, allocate and free logs
  template <typename T, int Size, int type, bool clean=true>
  struct Log {
    int top = 0;
    T entries[Size];
    void reset() {
      if (clean) for (int i = 0; i < top; i++) entries[i] = T();
      top = 0;}
    inline void push_back(const T x) {
      if (top + 1 >= Size) {
        std::cout << "ustm: overfull log: type "  << type << std::endl;
        std::abort();  }
      entries[top++] = x;}
    T& operator [] (long i) { return entries[i]; }
  };

  // Transaction descriptor, includes the logs and other state
  struct Descriptor {
    static constexpr int LogSize = 1ul << 12; // size of the logs
    std::jmp_buf env;
    uint64_t tid;
    uint64_t numAborts {0};
    uint64_t numCommits {0};
    uint64_t numRetries {0};    
    TS startStamp;

    // Filter for fast check if entry in write log.  Can return false positives.
    std::bitset<1024> filter;

    // Following four entries are the logs
    // Keeps the pointer and a function that writes the value to given location.
    Log<std::tuple<void*,TS,std::function<void(void*)>>, LogSize, 0> writeLog;
    // Read log keeps pointer to locations.  Used to later validate reads.
    Log<const void*, 128*LogSize, 1, false> readLog;
    // alloc/delete logs keep functions that delete values on abort/success
    Log<std::function<void()>, LogSize, 2> allocLog;
    Log<std::function<void()>, LogSize, 3> deleteLog;

    void reset() {
      writeLog.reset(); readLog.reset();
      deleteLog.reset(); allocLog.reset(); filter.reset();
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
      for (int i = 0; i < numLocks; i++) writeLocks[i].store(Status(1));}

    // Returns a lock id in [0 .. numLocks) based on hash of the address
    uint64_t getLockNum(const void* addr) {
      return hashAddress(addr) & (numLocks - 1); }
    
    // Returns 0 if self locked, timestamp if succeeded, or {} if
    // lock is already locked.   Will fail if either locked (by another) or
    // timestamp on lock is after startStamp.
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

    // Wrap the following around a load to check if it was safe (no
    // updates during load).
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
      if (myd->writeLog.top > 0) {
        // Take locks (abort if any fail)
        for (int i = 0; i < myd->writeLog.top; i++) {
          auto r = locks.tryLock(std::get<0>(myd->writeLog[i]), myd->tid,
                                 myd->startStamp);
          if (!r.has_value()) abortTransaction(myd);
          std::get<1>(myd->writeLog[i]) = *r;
        }
        TS endStamp = getStamp();
        // Validate the reads (abort if any fail)
        for (int i = 0; i < myd->readLog.top; i++)
          if (!locks.validateRead(myd->readLog[i], myd->tid, myd->startStamp)) 
            abortTransaction(myd);
        // If here (locks and validates succeeded) then transaction succeeded.
        // Apply the writes and then unlock them.
        for (int i = 0; i < myd->writeLog.top; i++) 
          std::get<2>(myd->writeLog[i])(std::get<0>(myd->writeLog[i]));
        std::atomic_thread_fence(std::memory_order_release);
        for (int i = 0; i < myd->writeLog.top; i++)
          if (std::get<1>(myd->writeLog[i]) != 0)
            locks.unlock(std::get<0>(myd->writeLog[i]), endStamp);
      }
      // Since successful, apply the deletes.
      for (uint64_t i = 0; i < myd->deleteLog.top; i++)
        uepoch::delay(std::move(myd->deleteLog[i]));
      myd->numCommits++;
      myd->reset();
      get_descriptor() = nullptr;
    }

    inline void abortTransaction(Descriptor* myd) {
      // Release locks and apply deletes to allocated objects.
      for (int i = 0; i < myd->writeLog.top; i++)
        if (std::get<1>(myd->writeLog[i]) != 0) 
          locks.unlock(std::get<0>(myd->writeLog[i]), 
                       std::get<1>(myd->writeLog[i]));
      for (int i = 0; i < myd->allocLog.top; i++)
        uepoch::delay(std::move(myd->allocLog[i]));
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
      if (myd->filter[hashAddress(&val) & 1023ul])
        for (int i = myd->writeLog.top - 1; i >= 0; i--)
          if (std::get<0>(myd->writeLog[i]) == &val) {
            typename std::remove_const<T>::type r;
            std::get<2>(myd->writeLog[i])((void*) &r);
            return r;
          }
      // Reads timestamp (status) before copying data to a local
      // buffer (tmp) and then again after.  Only succeeds if equal
      // and less than start stamp.
      uint64_t idx = locks.getLockNum(&val);
      auto status = locks.loadStart(idx);
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
      if (myd == nullptr) loc = v;
      else {
        myd->filter[hashAddress(&loc) & 1023ul] = 1;
        myd->writeLog.push_back(std::tuple(&loc, 0, [=] (void* p) {
                                                      *((T*) p) = v;}));
      }
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
        return uepoch::with_epoch([&] {
          setjmp(myd->env);
          myd->startStamp = (myd->numRetries > 0) ? nextStamp() : getStamp();
          auto retval = func();
          commitTransaction(myd);
          return retval;});
      } else {
        uepoch::with_epoch([&] {
          setjmp(myd->env);
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
