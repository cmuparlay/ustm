#include <type_traits>
#include <mvustm.h>

namespace stm {
  
  extern inline ustm::STM& getStm() {
    static ustm::STM stm;
    return stm;
  }

  template <typename F>
  auto atomic_region(F &&func) {return getStm().transaction(func); }
  template <typename F>
#ifdef MV
  auto atomic_read_only(F &&func) { return getStm().transaction(func, true); }
#else
  auto atomic_read_only(F &&func) { return atomic_region(func); }
#endif
  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    return uepoch::protect(func); }
  template <typename T>
  void Delete(T *ptr) { getStm().Delete(ptr); }
  template <typename T, typename... Args>
  T *New(Args... args) { return getStm().New<T>(args...); }

  template <typename T>
  struct atomic {
  private:
    T val;
  public:
    atomic(T v) : val(v) { }
    atomic()  {}
    void store(T v) { getStm().store(val, v); }
    T load() const { return getStm().load(val); }
#ifdef UnprotectedLoad
    T load_unprotected() const { return getStm().load(val, false); }
    void validate() const { load(); }
#else
    T load_unprotected() const { return load();}
    void validate() const { }
#endif
    T operator=(T b) { store(b); return b; }
  };

  struct versioned {};
}
