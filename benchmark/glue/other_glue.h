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

#define phytm1_fgl_colocated_locks_no_phtm  // Use tm_volatile.h instead of tm.h
#define DISABLE_ZEROING_MEMORY 
#define MAX_THREADS_POW2 512 // Increase thread limit for USTM benchmarks
#define LimitedSTM // Data must be allocated inside transaction

#include "../../other/ptmbench/phytmlib/phytm2.h"

namespace {
  struct HyTmThreadCheckInOut {
    bool initialized = false;
    HyTmThreadCheckInOut() {
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] Thread " << std::this_thread::get_id() << " initializing..." << std::endl;
#endif
      TM_THREAD_ENTER();
      initialized = true;
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] Thread " << std::this_thread::get_id() << " initialized successfully" << std::endl;
#endif
    }
    // Note: TM_THREAD_EXIT() might not be needed based on HyTm's design
    ~HyTmThreadCheckInOut() {
      if (initialized) {
#ifdef HYTM_VERBOSE_LOGGING
        std::cout << "[HyTm] Thread " << std::this_thread::get_id() << " cleaning up..." << std::endl;
#endif
      }
    }
  };
  
  thread_local HyTmThreadCheckInOut tl_hytm_tcico;
  
  // Global initializer
  struct HyTmInit {
    static void bus_error_handler(int sig) {
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] BUS ERROR detected during program execution!" << std::endl;
      std::cout << "[HyTm] This likely occurred during HyTm memory management operations" << std::endl;
      std::cout << "[HyTm] Signal: " << sig << std::endl;
#endif
      std::exit(1);
    }
    
    HyTmInit() {
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] Global initialization starting..." << std::endl;
#else
      // Suppress HyTm library output during initialization
      int stdout_backup = dup(STDOUT_FILENO);
      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
#endif
      
      // Install signal handler before HyTm initialization
      struct sigaction sa;
      sa.sa_handler = bus_error_handler;
      sigemptyset(&sa.sa_mask);
      sa.sa_flags = 0;
      sigaction(SIGBUS, &sa, nullptr);
      
      try {
        TM_STARTUP(2);  // Use only 2 threads for initialization
#ifdef HYTM_VERBOSE_LOGGING
        std::cout << "[HyTm] Global initialization completed" << std::endl;
#endif
      } catch (...) {
#ifdef HYTM_VERBOSE_LOGGING
        std::cout << "[HyTm] Exception during initialization - continuing" << std::endl;
#endif
      }
      
#ifndef HYTM_VERBOSE_LOGGING
      // Restore stdout
      dup2(stdout_backup, STDOUT_FILENO);
      close(stdout_backup);
#endif
    }
    ~HyTmInit() { 
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] Global shutdown starting..." << std::endl;
#else
      // Suppress HyTm library output during shutdown
      int stdout_backup = dup(STDOUT_FILENO);
      int devnull = open("/dev/null", O_WRONLY);
      dup2(devnull, STDOUT_FILENO);
      close(devnull);
#endif
      TM_SHUTDOWN(); 
#ifdef HYTM_VERBOSE_LOGGING
      std::cout << "[HyTm] Global shutdown completed" << std::endl;
#endif
#ifndef HYTM_VERBOSE_LOGGING
      // Restore stdout
      dup2(stdout_backup, STDOUT_FILENO);
      close(stdout_backup);
#endif
    }
  };
  static HyTmInit hytm_init;
}

namespace stm { struct versioned {}; }

#else
#error "No STM Specified"
#endif

#ifdef HyTm
// HyTm implementation
namespace stm {
  template <typename F>
  auto atomic_region(F &&func) {
    // Thread is automatically initialized via tl_hytm_tcico
    using T = decltype(func());
    if constexpr (std::is_void_v<T>) {
      TM_BEGIN();
      try {
        func();
        TM_END();
      } catch (...) {
#ifdef HYTM_VERBOSE_LOGGING
        std::cout << "[HyTm] Exception in atomic_region (void)" << std::endl;
#endif
        throw;
      }
    } else {
      T result;
      TM_BEGIN();
      try {
        result = func();
        TM_END();
      } catch (...) {
#ifdef HYTM_VERBOSE_LOGGING
        std::cout << "[HyTm] Exception in atomic_region (non-void)" << std::endl;
#endif
        throw;
      }
      return result;
    }
  }

  template <typename F>
  auto atomic_read_only(F &&func, bool large = false) {
    // HyTm has read-only transactions
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
// Non-HyTm STM implementations
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

  template <typename T>
  using atomic = typename ext_stm::template tmtype<T>;

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