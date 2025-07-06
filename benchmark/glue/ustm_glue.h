#include <type_traits>
#include <ustm.h>

namespace stm {
  template <typename F>
  auto atomic_region(F &&func) {return ustm::atomic_region(func); }
  template <typename F>
#ifdef MV
  auto atomic_read_only(F &&func) { return ustm::atomic_read_only(func); }
#else
  auto atomic_read_only(F &&func) { return ustm::atomic_region(func); }
#endif
  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    return uepoch::protect(func); }
  template <typename T>
  void Delete(T *ptr) { ustm::Delete(ptr); }
  template <typename T, typename... Args>
  T *New(Args... args) { return ustm::New<T>(args...); }

  template <typename T>
  struct atomic_other {
  private:
    T val;
  public:
    atomic_other(T v) : val(v) { }
    atomic_other()  {}
    void store(T v) { ustm::store(val, v); }
    T load() const { return ustm::load(val); }
#ifdef UnprotectedLoad
    T load_unprotected() const { return ustm::load_unprotected(val); }
    void validate() const { load(); }
#else
    T load_unprotected() const { return load();}
    void validate() const { }
#endif
    T operator=(T b) { store(b); return b; }
  };

  template <typename T>
  struct atomic_ptr {
  private:
    ustm::version_ptr<std::remove_pointer_t<T>> v;
  public:
    atomic_ptr(T v) : v(v) { }
    atomic_ptr() { }
    void store(T x) { v.store(x); }
    T load() { return v.load(); }
#ifdef UnprotectedLoad
    T load_unprotected() { return v.load_unprotected(); }
    void validate() { v.load(); }
#else
    T load_unprotected() { return load();}
    void validate()  { }
#endif
    T operator=(T b) { store(b); return b; }
  };

#ifdef MV
  template<typename T>
  using atomic = std::conditional_t<std::is_pointer_v<T>,
				    atomic_ptr<T>,
				    atomic_other<T>>;
  using versioned = ustm::version;
#else
  template<typename T>
  using atomic = atomic_other<T>;
  struct versioned {};
#endif
}
