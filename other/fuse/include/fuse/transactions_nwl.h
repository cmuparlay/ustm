#ifndef TLF_STM_H
#define TLF_STM_H

#define DOATOMIC 1

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

// The definition of atomic,
#include "atomic.h"

// The definition of versioned_ptr
#include "versioned_ptr.h"

namespace verlib {
  using tlf_internal::memory_pool;
  using tlf_internal::atomic_region;
  using tlf_internal::atomic_read_only;
  using tlf_internal::versioned;

  // locks are noops
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) {
      tlf_internal::in_lock++;
      auto result = f();
      tlf_internal::in_lock--;
      return result;
    }

    template <typename Thunk>
    auto try_lock(Thunk f) {
      tlf_internal::in_lock++;
      bool result = f();
      tlf_internal::in_lock--;
      return result;
    }
  };

  template <typename T>
  struct versioned_ptr : tlf_internal::lock {
  private:
    tlf_internal::versioned_ptr<T> v;
  public:
    versioned_ptr(T* v) : v(v) {}
    versioned_ptr() : v(nullptr) {}
    
    T* load() { return v.load(); }
    T* read_snapshot() { return load(); }
    void init(T* x) { v.init(x);}
    void store(T* x) {
      if (tlf_internal::in_constructor) v.init(x);
      else try_lock([&] {v.store(x); return true;});}
    T* operator=(T* b) {store(b); return b; }
  };

  struct atomic_bool : tlf_internal::lock {
  private:
    tlf_internal::atomic_bool v;
  public:
    atomic_bool(bool x) : v(x) {}
    bool load() { return read_lock([&] {return v.load();});}
    bool load_weak() { return v.load(); }
    void validate() { v.validate(); }
    void validate(bool x) { v.validate(); }
    void store(bool x) { try_lock([&] {v.store(x); return true;});}
    bool operator=(bool b) {store(b); return b; }
  };

}
#endif // TLF_STM_H
