#pragma once

#include <type_traits>
#include "../../other/multiverse/tm_common/tm_alloc2.h"
#include "../../other/multiverse/multiverse/multiverse.hpp"

namespace stm {
  struct versioned {};

  template <typename F>
  auto atomic_region(F &&func) {
    using R = std::invoke_result_t<F>;
    R result;
    ns_multiverse::updateTx([&] {
      result = func();
    });
    return result;
  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    using R = std::invoke_result_t<F>;
    R result;
    ns_multiverse::readTx([&] {
      result = func();
    });
    return result;
  }

  template <typename F>
  auto atomic_read_only_singleton(F f) {
    return atomic_read_only(std::forward<F>(f));
  }

  template <typename T>
  void Delete(T *ptr) { 
    ns_multiverse::tmDelete(ptr);
  }
      
  template <typename T, typename... Args>
  T *New(Args... args) { 
    return ns_multiverse::tmNew<T>(std::forward<Args>(args)...);
  }

  template <typename T>
  struct atomic {
    ns_multiverse::tx_field<T> val;
    
    atomic() : val() {}
    atomic(T v) : val() { val = v; }
    void store(T v) { val.store(v); }
    T load() { return val.load(); }
    T load_unprotected() { return val.load(); }
    void validate() const { }
    void init(T x) { val = x; }
    T operator=(T b) { val = b; return b; }
  };

  std::pair<long,long> statistics() {
    return {0ul, 0ul};
  }
}
