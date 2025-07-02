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
//     - T unprotected_load()  // can be same as load
//     - void validate()  // should be noop if no unprotected loads
//     - void store(T)
//     - void init(T)   // used before others have pointer to this
//     - T operator=(T)

#ifdef TWOPLSF
#include "glue/2plsf_glue.h"
#elif MVUSTM
#include "glue/mvustm_glue.h"
#else
#include "glue/ustm_glue.h"
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
