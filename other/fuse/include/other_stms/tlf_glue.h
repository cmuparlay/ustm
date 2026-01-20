#pragma once

#if defined TWOPLSF
#include "other_stms/2PLSF_modified.hpp"
namespace ext_stm = twoplsf;
#endif

namespace verlib {

  thread_local int in_lock = 0;

#ifdef SV_TLF_STM
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) {
      in_lock++;
      auto result = f();
      in_lock--;
      return result;
    }

    template <typename Thunk>
    auto try_lock(Thunk f) {
      in_lock++;
      bool result = f();
      in_lock--;
      return result;
    }
  };
#elif TWOPLSF // SV_TLF 
  struct lock {
    template <typename Thunk>
    auto read_lock(Thunk f) {
      ext_stm::try_read_lock(this);
      return f();
    }

    template <typename Thunk>
    auto try_lock(Thunk f) {
      ext_stm::try_write_lock(this);
      auto x = f();
      return x;
    }
  };
#endif
  
  template <typename F>
  auto atomic_region(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      ext_stm::updateTx([&] {in_lock = 0; func();});
    } else {
      return *ext_stm::updateTx<decltype(func())>([&] {in_lock = 0; return func();});
    }
  }

  template <typename F>
  auto atomic_read_only(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
     ext_stm::readTx([&] {in_lock = 0; func();});
    } else {
      return ext_stm::readTx<decltype(func())>([&] {in_lock = 0; return func();});
    }
  }

  template <typename F>
  auto with_snapshot(F f, bool unused_parameter = false)
  {
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

#ifdef TWOPLSF
#define stm_descriptor ext_stm::tl_opdata
#else
#define stm_descriptor ext_stm::get_descriptor()
#endif
  
  template <typename T>
  struct versioned_ptr {
  private:
    ext_stm::tmtype<T *> val;
  public:
    versioned_ptr(T *v) : val(v) {}
    versioned_ptr() : val(nullptr) {}
    void store(T *v) { val = v; }
    T *load() const {
      if (stm_descriptor == nullptr) [[likely]]  return val.val;
#ifdef SV_TLF_STM
      if (in_lock == 0)
        return val.pload_weak();
      else
#endif
        return val.pload();
    }
    T *load_weak() const {
      return val.pload_weak();
    }
    T *read_snapshot() { return load(); }
    void init(T *x) { store(x); }
    T *operator=(T *b) { store(b); return b; }
  };

  struct atomic_bool {
  private:
    ext_stm::tmtype<long> val;
  public:
    atomic_bool(bool v) : val(v) {}
    atomic_bool() : val(0) {}
    bool load() { return val.pload(); }
    void store(bool v) { val = v; }
    bool operator=(bool b) {store(b); return b; }
  };
}
