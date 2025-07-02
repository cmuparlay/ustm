#pragma once

#include "2PLSF.hpp"
namespace ext_stm = twoplsf;

namespace stm {

  template <typename F>
  auto atomic_region(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      ext_stm::updateTx(func);
    } else  {
      return ext_stm::updateTx<decltype(func())>(func);
    }
  }

  template <typename F>
  auto atomic_read_only(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
     ext_stm::readTx(func);
  } else {
      return ext_stm::readTx<decltype(func())>(func);
    }
  }

  template <typename F>
  auto atomic_read_only_singleton(F f) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      atomic_read_only(f);
    } else {
      return atomic_read_only(f);
    }
  }

  template <typename T>
  void Delete(T *ptr) { ext_stm::tmDelete(ptr); }
      
  template <typename T, typename... Args>
  T *New(Args... args) { return ext_stm::tmNew<T>(args...); }

  template <typename T>
  struct atomic {
  private:
    ext_stm::tmtype<T> val;
  public:
    atomic(T v) : val(v) {}
    atomic() : val(nullptr) {}
    void store(T v) { val = v; }
    T load() const { return val.pload(); }
    T load_unprotected() const { return val.pload();}
    void validate() const { }
    void init(T x) { store(x); }
    T operator=(T b) { store(b); return b; }
  };

}
