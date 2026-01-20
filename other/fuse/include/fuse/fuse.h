#ifndef TLF_DEFS_H_
#define TLF_DEFS_H_

#ifndef MV_TLF
#define MV_TLF
#endif

#include <shared_mutex>
#include "flock/flock.h"
#include "flock/acquired_pool.h"
#include "../verlib/timestamps.h"

// defines the transaction_descriptor structure
#include "transaction_descriptor.h"

// memory pool to handle aborted allocates and retires
#include "memory_pool.h"

// the main functions 
#include "core_functions.h"

// The lock interface
#include "lock.h"
#include "tlf_locks.h"

// The definition of atomic,
#include "atomic.h"

// The definition of versioned_ptr
#include "versioned_ptr.h"

// this is the full interface
namespace fuse {

  using flck::with_epoch;

  // currently only supports atomics that are pointers or bools
  using tlf_internal::atomic_region;
  using tlf_internal::atomic_read_only;
  using tlf_internal::shared_mutex;
  using tlf_internal::New;
  using tlf_internal::Retire;
  using tlf_internal::versioned;

  // some functions for managing the memory pool
  using tlf_internal::pool_clear;
  using tlf_internal::pool_stats;

  template <typename T>
  struct indirect : versioned {
    T value;
    indirect(T value) : value(value) {}
  };

  // if not a boolean or versioned pointer we need to use indirection
  template <typename T>
  struct indirect_atomic {
    tlf_internal::versioned_ptr<indirect<T>> ptr;
    indirect_atomic(T v) : ptr(New<indirect<T>>(v)) {}
    indirect_atomic() : ptr(nullptr) {}
    ~indirect_atomic() {Retire(ptr.load());}
    T load() { return ptr.load()->value; }
    void store(T v) {
      auto old = ptr.load();
      ptr.store(New<indirect<T>>(v));
      Retire(old);
    }
    T operator=(T b) {store(b); return b; }
  };
  
  template<typename T>
  using atomic = std::conditional_t<std::is_same_v<T, bool>,
				    tlf_internal::atomic_bool,
                                    std::conditional_t<std::is_pointer_v<T>,
                                                       tlf_internal::versioned_ptr<std::remove_pointer_t<T>>,
                                                       indirect_atomic<T>>>;

  template <typename T>
  struct tlf_atomic {
    atomic<T> v;
    shared_mutex lock;
    tlf_atomic(T v) : v(v) {}
    T load() {
      std::shared_lock lck(lock);
      return v.load(); }
    void store(T x) {
      std::unique_lock lck(lock);
      v.store(x);}
    T operator=(T b) {store(b); return b; }
  };

} // fuse


#endif // TLF_DEFS_H_
