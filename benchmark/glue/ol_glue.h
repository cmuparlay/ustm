// Glue that lets mixed.cpp drive the plain blocking optimistic-locking
// data structures from other/fuse/include/structures. These structures
// have no STM — their public operations (tr.insert / find / remove /
// upsert) each enter their own epoch internally for memory reclamation,
// so the outer stm::atomic_region wrapper is a pure pass-through. There
// are no transactional semantics for trans>1: each op inside the batch
// is individually linearizable, but the batch as a whole is not atomic.
// Range queries are not supported for OL — gated off in mixed.cpp.
//
// Also provides stm::atomic / stm::versioned as cheap aliases over
// flck primitives so the OL structures (compiled with OL_USE_STM)
// route their internal versioned_ptr/atomic_bool through stm::atomic.
// In this build that's just a typedef to flck::atomic, so semantics
// and overhead match the historical flck-direct code path. The point
// of routing through stm::atomic is to give the fuse glue
// (stm::atomic = fuse::tlf_atomic) a hook to participate in fuse
// transactions when the same structures are built into _ol_txn.
#pragma once

#include <utility>
#include <flock/flock.h>

namespace stm {

  template <typename T>
  using atomic = flck::atomic<T>;
  using lock = flck::lock;
  struct versioned {};

  // For OL-only builds (no STM), with_epoch routes to flck's epoch since
  // the structures' allocations are flck-based. Under uSTM/TLF builds the
  // glue overrides this to uepoch.
  template <typename F>
  auto with_epoch(F&& f) { return flck::with_epoch(std::forward<F>(f)); }

  // Structures call stm::New / stm::Delete directly (no memory_pool). For
  // the OL-only build these delegate to flck's per-type epoch-protected
  // pool (immediate malloc on New, epoch-deferred reclamation on Delete).
  template <typename T, typename... Args>
  T* New(Args&&... args) {
    return epoch::memory_pool_<T>::New(std::forward<Args>(args)...);
  }
  template <typename T>
  void Delete(T* p) { epoch::memory_pool_<T>::Retire(p); }

  template <typename F>
  auto atomic_region(F&& f, bool large = false) { return f(); }

  template <typename F>
  auto atomic_read_only(F&& f, bool large = false) { return f(); }

  template <typename F>
  auto atomic_read_only_singleton(F&& f, bool large = false) { return f(); }

  inline std::pair<long, long> statistics() { return {0, 0}; }
}
