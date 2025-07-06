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
    return getStm().transactionReadOnly(func); }

  template <typename F>
  auto atomic_read_only_singleton(F &&func) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      uepoch::protect(func);
    } else { return uepoch::protect(func); }
  }
      
  template <typename T>
  void Delete(T *ptr) { getStm().Delete(ptr); }
      
  template <typename T, typename... Args>
  T *New(Args... args) { return getStm().New<T>(args...); }

  template <typename T>
  struct atomic_ptr {
  private:
    ustm::version* val;
  public:
    atomic_ptr(T v) : val(v) { }
    atomic_ptr() { }
    void store(T v) { getStm().store(val, (ustm::version*) v); }
    T load() { return (T) getStm().load(val); }
#ifdef UnprotectedLoad
    T load_unprotected()  { return (T) getStm().load(val, false); }
    void validate() { load(); }
#else
    T load_unprotected() { return (T) load();}
    void validate()  { }
#endif
    T operator=(T b) { store(b); return b; }
  };
  
  struct Empty : ustm::version { Empty() {} };
  Empty true_struct;
  Empty* false_val = nullptr;
  Empty* true_val = &true_struct;
  
  // Simulates a boolean with a pointer to an "Empty" dummy struct.
  // If false, then pointer is nullptr otherwise pointer to "true_struct".
  // Avoids level of indirection when storing false.
  struct atomic_prim_bool {
    atomic_ptr<Empty*> val;
    bool load() { return val.load() == true_val; }
    void validate() { val.validate(); }
    void store(bool v) { val = (v ? true_val : false_val); }
    atomic_prim_bool(bool v) : val(v ? true_val : false_val) {}
    bool operator=(bool b) {store(b); return b; }
  };

  template <typename T>
  struct atomic_other {
  private:
    T val;
  public:
    atomic_other(T v) : val(v) {}
    atomic_other()  {}
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

  template<typename T>
  using atomic = std::conditional_t<std::is_pointer_v<T>,
				    atomic_ptr<T>,
				    atomic_other<T>>;

  using versioned = ustm::version;
}
