#include <type_traits>
#include <ustm.h>

namespace stm {
  extern inline ustm::STM& getStm() {
    static ustm::STM stm;
    return stm;
  }

  template <typename F>
  auto atomic_region(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      getStm().transaction(func);
    } else { return getStm().transaction(func); }
  }

  template <typename F>
  auto atomic_read_only(F &&func) { 
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
     atomic_region(func);
    } else { return atomic_region(func); }
  }

  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      uepoch::with_epoch(func);
    } else { return uepoch::with_epoch(func); }
  }
      
  template <typename T>
  void Delete(T *ptr) { getStm().Delete(ptr); }
      
  template <typename T, typename... Args>
  T *New(Args... args) { return getStm().New<T>(args...); }

  template <typename T>
  struct atomic {
  private:
    T val;
  public:
    atomic(T v) : val(v) {}
    atomic() {}
    void store(T v) { getStm().store(val, v); }
    T load() const { return getStm().load(val); }
#ifdef UnprotectedLoad
    T load_unprotected() const { return getStm().load(val, false); }
    void validate() const { load(); }
#else
    T load_unprotected() const { return load();}
    void validate() const { }
#endif
    void init(T x) { store(x); }
    T operator=(T b) { store(b); return b; }
  };

}
