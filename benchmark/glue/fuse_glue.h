#pragma once

#include "fuse/fuse.h"
#include <type_traits>
#include <optional>

namespace stm {

  using versioned = fuse::versioned;

  template<typename T>
  using atomic = fuse::tlf_atomic<T>;

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
    return {0ul, 0ul};
  }

}
