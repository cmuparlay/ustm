#pragma once

#include "fuse/fuse.h"
// fuse/fuse.h unconditionally #define MV_TLF for its own protocol, but
// the structures' verlib.h selects backend via an #elif chain where
// MV_TLF is checked before MV_STM / MV_TLF_STM. If we leave MV_TLF
// defined when the user explicitly asked for MV_STM or MV_TLF_STM, the
// structures still compile as MV_TLF. Strip it back out here.
#if defined(MV_STM) || defined(MV_TLF_STM)
#undef MV_TLF
#endif

// Belt-and-suspenders: catch any future regression where MV_TLF leaks
// back into a build that asked for a different fuse backend.
#if defined(MV_STM)
static_assert(true, "MV_STM build: MV_TLF must be undefined here");
#ifdef MV_TLF
#error "fuse_glue.h: MV_TLF must NOT be defined when MV_STM is set"
#endif
#endif
#if defined(MV_TLF_STM)
#ifdef MV_TLF
#error "fuse_glue.h: MV_TLF must NOT be defined when MV_TLF_STM is set"
#endif
#endif

#include "flock/flock.h"
#include <type_traits>
#include <optional>

namespace stm {

  using versioned = fuse::versioned;

  template<typename T>
  using atomic = fuse::tlf_atomic<T>;

  // Routed through verlib::lock under OL_USE_STM. Fuse's TLF protocol
  // intercepts flck::lock::try_lock directly (checks
  // tlf_internal::current_transaction); aliasing stm::lock to flck::lock
  // means OL operations participate in fuse's lock-fusion automatically.
  using lock = flck::lock;

  // Routed through verlib::with_epoch under OL_USE_STM. Fuse's
  // atomic_region already wraps in epoch protection internally, and
  // flck::with_epoch nests safely; this just makes the in-structure
  // epoch announce go through flck (fuse's native EBR).
  template <typename F>
  auto with_epoch(F&& f) { return flck::with_epoch(std::forward<F>(f)); }

  template <typename F>
  auto atomic_region(F &&func, bool large = false) {
    static_assert(std::is_invocable_v<F>);
    using R = std::invoke_result_t<F>;
    return fuse::atomic_region([&]() -> std::optional<R> {
      return func();
    });
  }

  template<typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    static_assert(std::is_invocable_v<F>);
    return fuse::atomic_read_only([&] {
      return func();
    });
  }

  template<typename F>
  auto atomic_read_only_singleton(F &&func, bool large = false) {
    return atomic_read_only(std::forward<F>(func), large);
  }

  template <typename T>
  void Delete(T *ptr) {
    fuse::Retire(ptr);
  }

  using fuse::New;

  std::pair<long,long> statistics() {
    return fuse::get_statistics();
  }

}
