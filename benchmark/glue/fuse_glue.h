#pragma once

#include "fuse/fuse.h"
#include "fuse/stm.h"
#include <optional>

namespace ext_stm {

  template<typename R, typename F>
  R updateTx(F&& func) {
    return fuse::atomic_region([&]() -> std::optional<R> {
      return func();
    });
  }

  template<typename F>
  void updateTx(F&& func) {
    fuse::atomic_region([&] { func(); });
  }

  // readTx with explicit return type
  template<typename R, typename F>
  R readTx(F&& func) {
    return fuse::atomic_read_only([&] { return func(); });
  }

  // readTx with void return
  template<typename F>
  void readTx(F&& func) {
    fuse::atomic_read_only([&] { func(); });
  }

  template<typename T, typename... Args>
  T* tmNew(Args&&... args) {
    return fuse::New<T>(std::forward<Args>(args)...);
  }

  template<typename T>
  void tmDelete(T* ptr) {
    fuse::Retire(ptr);
  }

  template<typename T>
  struct tmtype {
  private:
  
    // Fuse supports only pointer types
    using pointee_type = std::remove_pointer_t<T>;
    verlib::versioned_ptr<pointee_type> ptr;
  public:
    tmtype() : ptr() {}
    tmtype(T p) : ptr(p) {}
    
    T load() { return ptr.load(); }
    T load_unprotected() { return ptr.load_weak(); }
    void store(T p) { ptr.store(p); }
    void validate() { ptr.validate(); }
    void init(T p) { ptr.init(p); }
    T operator=(T p) { store(p); return p; }
  };

} // namespace ext_stm
