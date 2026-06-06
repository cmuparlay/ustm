#pragma once

#include <type_traits>
#ifdef OldSTM
#include <mvustm.h>
#else
#include <ustm_swiss.h>
#endif
#ifdef USTM_TLF
// Used to wrap the OL structures' flck::lock with the TLF counter-variant
// behavior. Only needed when OL_USE_STM routes verlib::lock through stm::.
#include <flock/flock.h>
#endif
namespace stm {

  template <typename F>
  auto atomic_region(F &&func) {
    return ustm::transaction(std::forward<F>(func)); }
  template <typename F>
#ifdef MV
  auto atomic_read_only(F &&func, bool large = false) {
    return ustm::transaction(std::forward<F>(func), true); }
#else
  auto atomic_read_only(F &&func, bool large = false) {
    return atomic_region(std::forward<F>(func)); }
#endif
  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    return atomic_read_only(std::forward<F>(func)); }
  template <typename T>
  void Delete(T *ptr) { ustm::Delete(ptr); }
  template <typename T, typename... Args>
  T *New(Args&&... args) { return
      ustm::New<T>(std::forward<Args>(args)...); }
  template <typename T>
  T load(const T& val) { return ustm::load(val); }
  template <typename T>
  void store(T& val, T v) { ustm::store(val, v); }

  template <typename T>
  struct direct_atomic {
    T val;
    // delete copy/move assignment/constructors
    direct_atomic(const direct_atomic&) = delete;
    direct_atomic& operator=(const direct_atomic&) = delete;
    direct_atomic(direct_atomic&& other) = delete;            
    direct_atomic& operator=(direct_atomic&& other) = delete; 

    direct_atomic(const T& v) : val(v) { }
    direct_atomic()  {}
    // Non-transactional initialization (e.g. during object construction
    // before any threads can see it). Matches flck::atomic::init.
    void init(const T& v) { val = v; }
    void store(const T& v) { ustm::store(val, v); }
    T load() const { return ustm::load(val); }
#ifdef UnprotectedLoad
    T load_unprotected() const { return ustm::load(val, true); }
    void validate() const { load(); }
#else
    T load_unprotected() const { return load();}
    void validate() const { }
#endif
    T operator=(T b) { store(b); return b; }
  };

  struct versioned {};

#ifdef USTM_INDIRECT
  template <typename T>
  struct indirect {
    T value;
    indirect(T value) : value(value) {}
  };

  template <typename T>
  struct indirect_atomic {
    indirect<T>* ptr;
    indirect_atomic(T v) : ptr(new indirect<T>(v)) {}
    indirect_atomic() : ptr(new indirect<T>(T{})) {}
    ~indirect_atomic() { delete ustm::load(ptr); }
    indirect_atomic(const indirect_atomic& other)
      : ptr(new indirect<T>(other.load())) {}
    indirect_atomic& operator=(const indirect_atomic& other) {
      if (this != &other) store(other.load());
      return *this;
    }
    T load() const { return ustm::load(ptr)->value; }
    void store(T v) {
      auto *old = ustm::load(ptr);
      ustm::store(ptr, ustm::New<indirect<T>>(v));
      ustm::Delete(old);
    }
    T operator=(T b) { store(b); return b; }
  };

  template<typename T>
  using atomic = std::conditional_t<sizeof(T) <= sizeof(void*),
                                    direct_atomic<T>, indirect_atomic<T>>;
#else
  template<typename T>
  using atomic = direct_atomic<T>;
#endif

  std::pair<long,long> statistics() {
#ifdef STMStats
    return ustm::statistics();
#else
    return std::pair(0ul,0ul);
#endif
  }

#ifdef USTM_TLF
  // verlib::lock under OL_USE_STM aliases to this. Wraps flck::lock with
  // the counter-variant TLF protocol — never acquires the underlying
  // lock inside a uSTM transaction; only bumps inProtected so load()
  // can decide whether to track reads.
  using lock = ustm::tlf_lock_counter<flck::lock>;
#else
  // OL_USE_STM may be on without USTM_TLF; provide a plain flck::lock
  // alias so the verlib::lock alias still resolves. This case takes the
  // lock as normal — useful only for compilation; semantically unsound
  // inside a uSTM transaction.
  using lock = flck::lock;
#endif

  // Structures call stm::New / stm::Delete directly. mvustm_glue already
  // provides stm::New (delegates to ustm::New, which adds to allocLog for
  // abort-undo) and stm::Delete (delegates to ustm::Delete, which posts
  // to deleteLog → uepoch::delay on commit). So nothing else needed here.

  // verlib::with_epoch routes through uSTM's uepoch so all in-transaction
  // operations are coordinated with the STM's timestamping protocol.
  template <typename F>
  auto with_epoch(F&& f) {
    return uepoch::protect(std::forward<F>(f));
  }
}
