#ifndef USTM_BENCH_H_
#define USTM_BENCH_H_
// An implementation of transactions must support
//   stm::atomic_region(F f)
//   stm::atomic_read_only(F f)  -- can be the same as above
//   stm::atomic_read_only_singleton(F f)  -- can be the same as above
//
//   struct stm::atomic<T>;
//     - atomic()  // constructors
//     - atomic(T v)
//     - T load()
//     - T load_unprotected()  // can be same as load
//     - void validate()  // should be noop if no unprotected loads
//     - void store(T)
//     - T operator=(T)

#if defined(USTM)
#include "glue/mvustm_glue.h"
#elif defined(USTM_)
#include "glue/ustm_glue.h"
#elif defined(Other)
#include "glue/other_glue.h"
#endif

namespace stm {
// If not using unprotected loads then atomic_bool does nothing
// otherwise it is an atomic<bool>
#ifdef UnprotectedLoad
  using atomic_bool = atomic<bool>;
#else
  struct atomic_bool {
  public:
    atomic_bool(bool x)  {}
    bool load() { return false;}
    void validate() { }
    void store(bool x) { }
    bool operator=(bool b) {store(b); return b; }
  };
#endif
}

#endif // USTM_BENCH_H_
