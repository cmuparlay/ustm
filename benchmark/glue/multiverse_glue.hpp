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
  struct direct_atomic {
    ns_multiverse::tx_field<T> val;
    
    direct_atomic() = default;
    direct_atomic(T v) : val(v) { }
    void store(T v) { val.store(v); }
    T load() { return val.load(); }
    T load_unprotected() { return val.load(); }
    void validate() const { }
    void init(T x) { val = x; }
    T operator=(T b) { val = b; return b; }
  };

  template <typename T>
  struct indirect : versioned {
    T value;
    indirect(T value) : value(value) {}
  };

  template <typename T>
  struct indirect_atomic {
    ns_multiverse::tx_field<indirect<T>*> ptr;
    indirect_atomic(T v) : ptr(new indirect<T>(v)) { }
    indirect_atomic() : ptr(new indirect<T>(T{})) { }
    indirect_atomic(const indirect_atomic& other) : ptr(new indirect<T>(const_cast<indirect_atomic&>(other).load())) {}
    indirect_atomic& operator=(const indirect_atomic& other) {
      if (this != &other) {
        store(const_cast<indirect_atomic&>(other).load());
      }
      return *this;
    }
    // ~indirect_atomic() {
    //   auto *p = ptr.load();
    //   if (p) delete p;
    // }
    T load() { return ptr.load()->value; }
    void store(T v) {
      auto *old = ptr.load();
      ptr.store(New<indirect<T>>(v));
      if (old) Delete(old);
    }
    T operator=(T b) {
      store(b);
      return b;
    }
    void validate() { }
  };

  template <typename T>
  using atomic = std::conditional_t<sizeof(T) <= sizeof(void*) && std::is_scalar_v<T>,
                                    direct_atomic<T>, indirect_atomic<T>>;

  std::pair<long,long> statistics() {
    return {0ul, 0ul};
  }
}
