#ifndef TLF_MEMORY_POOL_H_
#define TLF_MEMORY_POOL_H_

namespace tlf_internal {

  thread_local bool in_constructor = false;
  
  template <typename xT>
  struct memory_pool {
    using T = xT;
    flck::memory_pool<T> pool;
    void reserve(size_t n) { pool.reserve(n);}
    //void shuffle(size_t n) { pool.shuffle(n);}
    void stats() { pool.stats();}
    void clear() { pool.clear();}
    void Delete(T* ptr) { pool.Delete(ptr); }

    bool is_helping() {
#ifndef FlockHelp
      return false;
#else
      return flck::internal::helping;
#endif
    }

    void retire_on_abort(transaction_descriptor* descriptor, T* ptr) {
      if (descriptor != nullptr && !is_helping()) {
	bool* x = pool.Retire(ptr);
	epoch::undo_Retire(x);
	descriptor->allocated_log.add(x);
      }
    }

    void retire_on_success(transaction_descriptor* descriptor, T* ptr) {
      bool* x = pool.Retire(ptr);
      if (descriptor != nullptr && !is_helping())
	descriptor->retired_log.add(x);
    }

    bool check_not_corrupted(T* ptr) {
      return pool.check_not_corrupted(ptr);
    }

    template <typename ... Args>
    T* New(Args... args) {
      bool hold = in_constructor;
      in_constructor = true;
      T* ptr = pool.New(args...);
      in_constructor = hold;
      retire_on_abort(current_transaction, ptr);
      return ptr;
    }

    void Retire(T* ptr) {
      retire_on_success(current_transaction, ptr);
    }
  };
  
  template <typename T>
  extern inline memory_pool<T>& get_pool() {
    static memory_pool<T> pool;
    return pool;
  }

  template <typename T, typename ... Args>
  inline T* New(Args... args) {
    return get_pool<T>().New(std::forward<Args>(args)...);}

  // f is a function that initializes a new object before it is shared
  template <typename T, typename F, typename ... Args>
  inline T* NewInit(const F& f, Args... args) {
    return get_pool<T>().new_init(f, std::forward<Args>(args)...);
  }

  template <typename T>
  inline void Delete(T* p) {get_pool<T>().Delete(p);}

  template <typename T>
  inline void Retire(T* p) {get_pool<T>().Retire(p);}

  template <typename T>
  inline void pool_clear() {get_pool<T>().clear();}

  template <typename T>
  inline void pool_stats() {get_pool<T>().stats();}

} // tlf_internal

#endif // TLF_MEMORY_POOL_H_
