#pragma once

#include <type_traits>

#ifdef HyTmHTM
#define phytm1_fgl_colocated_locks_no_pmem  // Volatile hybrid TM
#else
#define phytm1_fgl_colocated_locks_no_phtm  // Volatile STM
#endif
#define DISABLE_ZEROING_MEMORY 
#define MAX_THREADS_POW2 512
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
      SuppressOuput SO;
      STM_STARTUP();
    }
    ~HyTmInit() { 
      SuppressOuput SO;
      TM_SHUTDOWN();
    }
  };
  static HyTmInit hytm_init;
}

namespace stm {
  struct versioned {};

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
    return atomic_read_only(std::forward<F>(f));
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
