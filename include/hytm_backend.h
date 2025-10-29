#pragma once

// This header allows using HyTm as the STM backend for USTM benchmarks
// Usage: compile with -DUSE_HYTM_BACKEND to replace USTM with HyTm

#ifdef USE_HYTM_BACKEND

// Include HyTm and make it behave like USTM
#include <type_traits>

#define DISABLE_PMEM
#define phytm1_fgl_colocated_locks_no_pmem

#include "../../other/ptmbench/phytmlib/phytm2.h"

namespace ustm {
  
  // Thread-local state
  thread_local bool hytm_thread_initialized = false;
  
  // Initialize HyTm thread if needed
  inline void ensure_thread_init() {
    if (!hytm_thread_initialized) {
      TM_THREAD_ENTER();
      hytm_thread_initialized = true;
    }
  }
  
  // Transaction operations to match USTM interface
  template <typename F>
  auto atomic_region(F &&func) {
    ensure_thread_init();
    using RetType = decltype(func());
    
    if constexpr (std::is_void_v<RetType>) {
      TM_BEGIN();
      func();
      TM_END();
    } else {
      RetType result;
      TM_BEGIN();
      result = func();
      TM_END();
      return result;
    }
  }
  
  template <typename F>
  auto atomic_read_only(F &&func) {
    ensure_thread_init();
    using RetType = decltype(func());
    
    if constexpr (std::is_void_v<RetType>) {
      TM_BEGIN_RO();
      func();
      TM_END();
    } else {
      RetType result;
      TM_BEGIN_RO();
      result = func();
      TM_END();
      return result;
    }
  }

  // Memory management
  template <typename T>
  void Delete(T *ptr) { 
    if (ptr != nullptr) {
      tmDelete(ptr);
    }
  }
      
  template <typename T, typename... Args>
  T *New(Args... args) { 
    return tmNew<T>(args...);
  }
  
  // Load/store operations - map to HyTm tx_field
  template <typename T>
  T load(tx_field<T>& field) {
    return field.load();
  }
  
  template <typename T>
  T load_unprotected(tx_field<T>& field) {
    return field.load_unsafe();
  }
  
  template <typename T>
  void store(tx_field<T>& field, const T& val) {
    field.store(val);
  }
  
  // For regular variables (non-transactional), just access directly
  template <typename T>
  T load(T& var) {
    return var;
  }
  
  template <typename T>
  T load_unprotected(T& var) {
    return var;
  }
  
  template <typename T>
  void store(T& var, const T& val) {
    var = val;
  }

  // Version pointer wrapper for compatibility
  template <typename T>
  struct version_ptr {
    tx_field<T*> ptr;
    
    version_ptr(T* p = nullptr) { ptr.store(p); }
    void store(T* p) { ptr.store(p); }
    T* load() { return ptr.load(); }
    T* load_unprotected() { return ptr.load_unsafe(); }
  };
  
  // Version type for compatibility
  using version = int; // HyTm handles versioning internally
}

// Global initialization/cleanup
namespace {
  struct HyTmInitializer {
    HyTmInitializer() {
      TM_STARTUP(128); // Max threads
    }
    ~HyTmInitializer() {
      std::cout << "Done\n";
      TM_SHUTDOWN();
    }
  };
  
  // Global instance ensures proper initialization/cleanup
  static HyTmInitializer hytm_init;
}

#endif // USE_HYTM_BACKEND