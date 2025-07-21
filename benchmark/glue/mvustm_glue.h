#include <type_traits>
#include <mvustm_nolngjmp.h>
namespace stm {

  template <typename F>
  auto atomic_region(F &&func) {return ustm::transaction(func); }
  template <typename F>
#ifdef MV
  auto atomic_read_only(F &&func, bool large = false) { return ustm::transaction(func, true, large); }
#else
  auto atomic_read_only(F &&func, bool large = false) { return atomic_region(func, false, large); }
#endif
  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    return uepoch::protect(func); }
  template <typename T>
  void Delete(T *ptr) { ustm::Delete(ptr); }
  template <typename T, typename... Args>
  T *New(Args... args) { return ustm::New<T>(args...); }
  template <typename T>
  T load(T& val) { return ustm::load(val); }
  template <typename T>
  void store(T& val, T v) { ustm::store(val, v); }

  template <typename T>
  struct atomic {
  private:
  public:
    T val;
    atomic(const T& v) : val(v) { }
    atomic()  {}
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
}
