// Glue that lets mixed.cpp drive the plain blocking optimistic-locking
// data structures from other/fuse/include/structures. These structures
// have no STM — their public operations (tr.insert / find / remove /
// upsert) each enter their own epoch internally for memory reclamation,
// so the outer stm::atomic_region wrapper is a pure pass-through. There
// are no transactional semantics for trans>1: each op inside the batch
// is individually linearizable, but the batch as a whole is not atomic.
// Range queries are not supported for OL — gated off in mixed.cpp.
#pragma once

#include <utility>

namespace stm {

  template <typename F>
  auto atomic_region(F&& f, bool large = false) { return f(); }

  template <typename F>
  auto atomic_read_only(F&& f, bool large = false) { return f(); }

  template <typename F>
  auto atomic_read_only_singleton(F&& f, bool large = false) { return f(); }

  inline std::pair<long, long> statistics() { return {0, 0}; }
}
