#ifndef TLF_LOCK_I_H_
#define TLF_LOCK_I_H_

#include<optional>

namespace tlf_internal {

struct shared_mutex {
  flck::lock lck;
  bool try_lock() {
    if (tlf_internal::current_transaction != nullptr) {
      tlf_internal::current_transaction->lock_log.add(lck.get_lock());
      tlf_internal::in_lock++;
      return true;
    } else return lck.try_lock_no_unlock();
  }

  void lock() {
    if (tlf_internal::current_transaction != nullptr) {try_lock();}
    else lck.lock_no_unlock();
  }

  void unlock() {
    if (tlf_internal::current_transaction != nullptr) 
      tlf_internal::in_lock--;
    else lck.unlock();
  }

  bool try_lock_shared() {
    if (tlf_internal::current_transaction != nullptr) {
      tlf_internal::in_lock++;
      return true;
    } else return true;
  }

  void lock_shared() { try_lock_shared();}

  void unlock_shared() {
    if (tlf_internal::current_transaction != nullptr) tlf_internal::in_lock--;
  }

};

} // namespace tlf_internal

#endif // TLF_LOCK_I_H_
