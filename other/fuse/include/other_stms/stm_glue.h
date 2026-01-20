#pragma once

#include "parlay/thread_specific.h"
#if defined TWOPLSF
#include "other_stms/2PLSF_modified.hpp"
namespace ext_stm = twoplsf;
#elif defined TINYSTM
#include "other_stms/TinySTM.hpp"
struct Record  { tinystm::tmtype<uint64_t> data[12]; };
namespace ext_stm = tinystm;
#elif defined ONEFILE
#include "other_stms/OneFileWF.hpp"
namespace ext_stm = ofwf;
#elif defined TL2O
#include "other_stms/TL2Orig.hpp"
namespace ext_stm = tl2orig;
struct Record  { tl2orig::tmtype<uint64_t> data[12]; };
#endif

namespace verlib {

  template <typename F>
  auto atomic_region(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      ext_stm::updateTx(func);
    } else  {
      return *ext_stm::updateTx<decltype(func())>(func);
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
  auto with_snapshot(F f, bool unused_parameter = false) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      atomic_read_only(f);
    } else {
      return atomic_read_only(f);
    }
  }

  struct versioned { };

  template <typename A, typename B, typename C>
  bool validate(const A &a, const B &b, const C &c) { return true; }
  
  template <typename T, typename... Args>
  T *stm_new(Args... args) {
    return ext_stm::tmNew<T>(args...);
  }

  template <typename T>
  struct memory_pool
  {
    void reserve(size_t n) {}
    void stats() {}
    void clear() {}
    void Delete(T *ptr) { Retire(ptr); }
    void Retire(T *ptr) { ext_stm::tmDelete(ptr); }
    template <typename... Args>
    T *New(Args... args) { return ext_stm::tmNew<T>(args...); }
    void shuffle(size_t n) { }
  };

  // locks are noops
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) { return f(); }

    template <typename Thunk>
    auto try_lock(Thunk f) { return f(); }
  };

  template <typename T>
  struct versioned_ptr {
  private:
    ext_stm::tmtype<T *> val;

  public:
    versioned_ptr(T *v) : val(v) {}
    versioned_ptr() : val(nullptr) {}
    void store(T *v) { val = v; }
    T *load() const { return val.pload(); }
#ifdef WeakLoad
    T *load_weak() const { return val.pload_weak();}
    void validate(T* v) { load(); }
#else
    T *load_weak() const { return load();}
    void validate(T* v) { }
#endif
    //void validate() {}
    T *read_snapshot() { return load(); }
    void init(T *x) { store(x); }
    T *operator=(T *b) { store(b); return b; }
  };

#ifdef WeakLoad
  struct atomic_bool {
  private:
    ext_stm::tmtype<long> val;
  public:
    atomic_bool(bool v) : val(v) {}
    atomic_bool() : val(0) {}
    bool load() { return val.pload(); }
    void validate(bool v) { load();}
    void store(bool v) { val = v; }
    bool operator=(bool b) {store(b); return b; }
  };
#else
  struct atomic_bool {
  public:
    atomic_bool(bool x)  {}
    bool load() { return false;}
    void validate() {}
    void validate(bool v) { }
    void store(bool x) { }
    bool operator=(bool b) {store(b); return b; }
  };
#endif

}
