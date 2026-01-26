#pragma once

#include <utility>
#include <type_traits>
#include <iostream>
#include <thread>
#include <csignal>
#include <cstdlib>
#include <unistd.h>
#include <fcntl.h>

#ifdef TL2O
#include "TL2Orig.hpp"
namespace ext_stm = tl2orig;
namespace stm { struct versioned {}; }
#define LimitedSTM

#elif defined(Tiny)
#include "TinySTM.hpp"
namespace ext_stm = tinystm;
namespace stm { struct versioned {}; }
#define LimitedSTM

#elif defined(OneFile)
#include "OneFileWF.hpp"
namespace ext_stm = ofwf;
namespace stm { using versioned = ofwf::tmbase; }

#elif defined(TWOPLSF)
#include "2PLSF.hpp"
namespace stm { struct versioned {}; }
namespace ext_stm = twoplsf;

#elif defined(Fuse)
#include "fuse_glue.h"
namespace stm { using versioned = fuse::versioned; }

#elif defined(HyTm)

#ifdef HyTmHTM
#define phytm1_fgl_colocated_locks_no_pmem  // HTM enabled, no PMEM
#else
#define phytm1_fgl_colocated_locks_no_phtm  // Software-only STM
#endif
#define DISABLE_ZEROING_MEMORY 
#define MAX_THREADS_POW2 512 // Increase thread limit for USTM benchmarks
#define LimitedSTM // Data must be allocated inside transaction

#include "../../other/ptmbench/phytmlib/phytm2.h"

namespace {

  // RAII class that suppresses output to a given fd
  struct SuppressOuput {
    int backup;
    int fd;
    SuppressOuput(int fd = STDOUT_FILENO) : fd(fd), backup(dup(fd)) {
      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, fd);
      close(devnull);
    }
    ~SuppressOuput() {
      dup2(backup, fd);
      close(backup);
    }
  };
  
  struct HyTmInit {
    HyTmInit() {
      // Suppress HyTm library output during initialization
      SuppressOuput SO;

      STM_STARTUP();
    }
    ~HyTmInit() { 
      // Suppress HyTm library output during shutdown
      SuppressOuput SO;

      TM_SHUTDOWN();
    }
  };
  static HyTmInit hytm_init;
}

namespace stm { struct versioned {}; }

#else
#error "No STM Specified"
#endif

#ifdef HyTm
namespace stm {
  template <typename F>
  auto atomic_region(F &&func) {
    using T = decltype(func());
    if constexpr (std::is_void_v<T>) {
      TM_BEGIN();
      func();
      TM_END();
    } else {
      T result;
      TM_BEGIN();
      result = func();
      TM_END();
      return result;
    }
  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    using T = decltype(func());
    if constexpr (std::is_void_v<T>) {
      TM_BEGIN_RO();
      func();
      TM_END();
    } else {
      T result;
      TM_BEGIN_RO();
      result = func();
      TM_END();
      return result;
    }
  }
  
  template <typename F>
  auto atomic_read_only_singleton(F &&f) {
    return atomic_region(std::forward<F>(f));
  }

  template <typename T>
  void Delete(T *ptr) { 
    tmDelete(ptr);
  }
      
  template <typename T, typename... Args>
  T *New(Args... args) { 
    return tmNew<T>(std::forward<Args>(args)...);
  }

  template <typename T>
  struct atomic {
    tx_field<T> val;
    
    atomic() : val() {}
    atomic(T v) : val() { val = v; }
    void store(T v) { val.store(v); }
    T load() const { return const_cast<tx_field<T>&>(val).load(); }  // Cast away const for load
    T load_unprotected() const { return const_cast<tx_field<T>&>(val).load(); }
    void validate() const { /* no-op */ }
    void init(T x) { val = x; }
    T operator=(T b) { val = b; return b; }
    operator T() const { return const_cast<tx_field<T>&>(val).load(); }
  };

  template <typename T>
  T load_unprotected(const tx_field<T>& field) {
    return field.load();
  }

  template <typename T>
  void validate(const tx_field<T>& field) {
    // No validation needed for HyTm
  }

  template <typename T>
  void init(tx_field<T>& field, T value) {
    field = value;
  }

  std::pair<long,long> statistics() {
    return {0ul, 0ul};
  }
}

#else
// Non-HyTm
namespace stm {

#ifdef OneFile
  template <typename F>
  auto atomic_region(F &&func) {
    using T = decltype(func());
    T result;
    ext_stm::updateTx<bool>([&] {result = func(); return true;});
    return result;
  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    using T = decltype(func());
    T result;
    ext_stm::readTx<bool>([&] {result = func(); return true;});
    return result;
  }
#else
  template <typename F>
  auto atomic_region(F &&func) {
    return ext_stm::updateTx<decltype(func())>(func);  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    return ext_stm::readTx<decltype(func())>(func);  }
#endif
  
  template <typename F>
  auto atomic_read_only_singleton(F f) {
      return atomic_region(f);
  }

  template <typename T>
  void Delete(T *ptr) { ext_stm::tmDelete(ptr); }
      
  template <typename T, typename... Args>
  T *New(Args... args) { return ext_stm::tmNew<T>(args...); }

  // Use the backend's tmtype directly for Fuse (it already exposes load/store/validate)
#if defined(Fuse)
  // Fuse's tmtype only supports pointer types (it wraps a versioned_ptr<pointee>).
  // Enforce this at compile time to avoid accidental value-type instantiations.
  template <typename T>
  struct atomic {
    static_assert(std::is_pointer<T>::value,
                  "Fuse backend: stm::atomic<T> supports only pointer types. Convert T to a pointer or use a different backend.");
    using impl = typename ext_stm::template tmtype<T>;
    impl val;
    atomic() = default;
    atomic(T p) : val(p) {}
    T load() const { return const_cast<impl&>(val).load(); }
    T load_unprotected() const { return const_cast<impl&>(val).load_unprotected(); }
    void store(T p) { val.store(p); }
    void validate() const { const_cast<impl&>(val).validate(); }
    void init(T p) { val.init(p); }
    T operator=(T p) { store(p); return p; }
    operator T() const { return val.load(); }
  };
#else
  template <typename T>
  struct atomic {
    ext_stm::tmtype<T> val;
    atomic() = default;
    atomic(T v) { val = v; }
    void store(T v) { val = v; }
    T load() const { return (T)val; }
    T load_unprotected() const { return load(); }
    void validate() const { }
    void init(T x) { val = x; }
    T operator=(T b) { val = b; return b; }
    operator T() const { return (T)val; }
  };
#endif

  std::pair<long,long> statistics() {
#if defined(STMStats) && defined(TWOPLSF)
    auto& stm = twoplsf::gSTM;
    uint64_t totalAborts = 0;
    uint64_t totalCommits = 0;
    for (int i=0; i < twoplsf::REGISTRY_MAX_THREADS; i++) {
      auto& d = stm.opDesc[i];
      totalAborts += d.numAborts;
      totalCommits += d.numCommits;
      d.numAborts = d.numCommits = 0;
    }
    return std::pair(totalAborts, totalCommits);
#else
    return std::pair(0ul,0ul);
#endif
  }
}

#endif // !HyTm