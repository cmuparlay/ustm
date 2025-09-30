#pragma once

#include <string.h>
#include <cassert>

#define START_TRANS __transaction_atomic{
#define END_TRANS }
#define TRANS_SAFE transaction_safe

#ifndef NDEBUG
//TODO: this might just abort a transaction and exit silently. Check how this works.
#define trans_assert(cond) if(!(cond)) { std::abort(); }
#else
#define trans_assert(cond)
#endif

//void* operator new(std::size_t, std::align_val_t) can not be called inside
// a transaction, so we implement our own aligned allocation.

static void* alloc_aligned_with_header(size_t size, size_t align) {
    trans_assert(align >= sizeof(void*));
    size_t header = sizeof(void*);
    //Allocate enough space for the object, alignment guarantees, and a header 
    size_t total = size + align + header;
    void* ptr = malloc(total);
    uintptr_t raw = reinterpret_cast<uintptr_t>(ptr) + header;
    uintptr_t aligned = (raw + (align - 1)) & ~(uintptr_t)(align - 1);
    //The header stores the ptr returned by malloc so we can free it later
    //We can store it after the object since free_with_header will know object size beforehand.
    void** header_addr = reinterpret_cast<void**>(aligned + size);
    *header_addr = ptr;
    return reinterpret_cast<void*>(aligned);
}

static void free_with_header(void* p, size_t size) noexcept {
    if (!p) return;
    void** store = reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(p) + size);
    void* ptr = *store;
    free(ptr);
}


namespace gcctm {
  //Keep tmDelete and tmNew inside of a transaction as (I believe) these might be
  // called outside a transaction and we want these calls to be instrumented.
  template<typename T>
  static void tmDelete(T* obj) {
    START_TRANS
      obj->~T();
      free_with_header(obj, sizeof(T));
    END_TRANS
  }

  template <typename T, typename... Args>
  static T* tmNew(Args&&... args) {
    START_TRANS
      void* p = alloc_aligned_with_header(sizeof(T), alignof(T));
      T* t = new (p) T(std::forward<Args>(args)...);
      return t;
    END_TRANS
  }

  // T can be any 64 bit type
  template<typename T>
  struct tmtype {
    //type needs to be trivially copyable as gcc does not allow
    // new to be called inside a transaction (constructor must
    // be declared transaction safe manually).
    static_assert(std::is_trivially_copyable_v<T> == true);

    T val;

    tmtype() { }
    tmtype(T initVal) : val(initVal) {}
    // Casting operator
    operator T() {  return pload();  }
    // Casting to const
    operator T() const {  return pload();  }
    // Prefix increment operator: ++x
    void operator++ () {  pstore(pload()+1);  }
    // Prefix decrement operator: --x
    void operator-- () {  pstore(pload()-1);  }
    void operator++ (int) {  pstore(pload()+1);  }
    void operator-- (int) {  pstore(pload()-1);  }
    tmtype<T>& operator+= (const T& rhs) {  pstore(pload() + rhs); return *this;  }
    tmtype<T>& operator-= (const T& rhs) {  pstore(pload() - rhs); return *this;  }
    // Equals operator
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator == (const tmtype<Y> &rhs) {  return pload() == rhs;  }
    // Difference operator: first downcast to T and then compare
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator != (const tmtype<Y> &rhs) {  return pload() != rhs;  }
    // Relational operators
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator < (const tmtype<Y> &rhs) {  return pload() < rhs;  }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator > (const tmtype<Y> &rhs) {  return pload() > rhs;  }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator <= (const tmtype<Y> &rhs) {  return pload() <= rhs;  }
    template <typename Y, typename = typename std::enable_if<std::is_convertible<Y, T>::value>::type>
    bool operator >= (const tmtype<Y> &rhs) {  return pload() >= rhs;  }
    // Operator arrow ->
    T operator->() {  return pload();  }
    // Copy constructor
    tmtype<T>(const tmtype<T>& other) {  pstore(other.pload());  }
    // Operator &
    T* operator&() {  return (T*)this;  }
    // Assignment operator from a persist<T>
    tmtype<T>& operator=(const tmtype<T>& other) { pstore(other.pload()); return *this; }
    // Assignment operator from a value
    tmtype<T>& operator=(T value) { pstore(value); return *this; }

    inline void pstore(T newVal) { val = newVal; }
    inline T pload() const { return val; }
  };

  template<typename R, typename F>
  static inline R Tx(F&& func) {
      R r;
    START_TRANS
      //try {
        r = func();
      //} catch (...) {
      //  std::abort();
      //}
    END_TRANS
      return r;
  }

  template<typename R, typename F> static R updateTx(F&& func) { return Tx<R,F>(func); }
  template<typename R, typename F> static R readTx(F&& func) { return Tx<R,F>(func); }

} //namespace gcctm
