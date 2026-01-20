#ifndef TLF_LOCK_H_
#define TLF_LOCK_H_

#include<algorithm>

namespace tlf_internal {

//thread_local bool in_lock;

struct lock {
private:
  flck::lock lck;
public:
  bool is_locked() { return lck.is_locked();}
  template <typename Thunk>
  auto read_lock(Thunk f) {
    using RT = decltype(f());
    if (current_transaction != nullptr) {
      in_lock++;
      RT result = f();
      in_lock--;
      return result;
    } else return f();
  }

  template <typename Thunk>
  auto try_lock(Thunk f) {
    if (current_transaction != nullptr) {
      //if (lck.is_locked() && !lck.is_self_locked()) return false;
      current_transaction->lock_log.add(lck.get_lock());
      in_lock++;
      bool result = f();
      in_lock--;
      return result;
    } else return lck.try_lock(f);
  }

  template <typename Thunk>
  auto try_lock_no_delay(Thunk f) {return try_lock(f);}
};

#ifndef FlockHelp
template <typename F>
bool run_with_locks_rec(int i, transaction_descriptor* descriptor, F f, bool do_help) {
  if (i == descriptor->lock_log.size()) return f();
  return descriptor->lock_log[i]->try_lock([=] {
    return run_with_locks_rec(i+1, descriptor, f, do_help);});
}
#else
template <typename F>
bool run_with_locks_rec(int i, transaction_descriptor* descriptor, F f, bool do_help) {
  int num_locks = descriptor->lock_log.size();
  if (num_locks == 0) return f();
  auto d = flck::internal::get_descriptor_pool().New();
  d->init(descriptor->lock_log.data(), num_locks);
  bool r = flck::internal::lock::multi_try_lock_(d, descriptor->lock_log.data(), num_locks, f, do_help);
  if (d->acquired.load())
    descriptor->acquired = true;
  flck::internal::get_descriptor_pool().Retire(d);
  return r;
}
#endif
  
} // namespace tlf_internal

#endif // TLF_LOCK_H_
