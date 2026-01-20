// Main code for atomic region
// Includes:
//   with_transaction(F f), which calls
//   commit(transaction_descriptor* descriptor), which calls
//   validate(transaction_descriptor* descriptor)

#ifndef TLF_ATOMIC_H_
#define TLF_ATOMIC_H_

#define TransStats

namespace tlf_internal {

#include <utility>
#include <algorithm>
#include <iostream>

thread_local TS start_stamp = 0;
thread_local long num_transactions = 0;
thread_local long num_aborts = 0;

// validate the versioned pointers that were read
void validate(transaction_descriptor* descriptor) {
  auto &log = descriptor->validate_ptr_log;
  bool passed = true;
  TS vts = descriptor->atomic_set_stamp(tbs, set_validating(global_stamp.get_write_stamp()));
  TS ts = get_validating_timestamp(vts);
  for (int i=0; i < log.size(); i++) {
    if (!is_validating_stamped(descriptor->time_stamp.load())) return;
    auto [location, expected] = log[i];
    if (!validate_pointer(location, expected, ts)) {
      passed = false; break;}
  }
  descriptor->atomic_set_stamp(vts, passed ? ts : failed);
}

void commit (transaction_descriptor* descriptor) {
  if (is_helping()) trans_descriptor_pool.acquire(descriptor);
  flck::skip_if_done([&] {
  assert(descriptor != nullptr);
  auto& write_log = descriptor->write_ptr_log;
  // Install writes at head of version lists
  std::pair<versioned*,versioned*> pointers[write_log.size()];

  int i = 0;
  for (; i < write_log.size(); i++) {
    auto [location, old_value, value] = write_log[i];
    pointers[i] = install_store(location, old_value, value, descriptor);
    if (pointers[i].second == nullptr) break;
  }
  if (i < write_log.size()) {
     descriptor->time_stamp = failed;
     for (int j=0; j < i; j++) {
       auto [location, old_value, value] = write_log[j];
       finalize_store(location, pointers[j], failed);
     }
    return;
  }

  // Set timestamp to go into validate mode
  descriptor->time_stamp = tbs; // logged load 

  // Validate the reads (not idempotent)
  flck::non_idempotent([=] {validate(descriptor);}); 
  TS ts = descriptor->time_stamp.load();

  // finalize logged writes (whether passed or failed)
  for (int i=0; i < write_log.size(); i++) {
    auto [location, old_value, value] = write_log[i];
    finalize_store(location, pointers[i], ts);
  }
  });  
}

// wrapper for a transaction
template <typename F>
auto atomic_(const F& f) {
  try_count = 0;
  int delay = 0;
  int max_delay = 0;
  num_transactions++;

  enum abortType : char {FailedSpeculative, FailedLock, FailedValidate};

  while (true) {
    abortType abort_type = FailedLock;
    auto r = flck::with_epoch([&] {
      in_lock = 0;
      abort_if_update = false;
      start_stamp = global_stamp.get_read_stamp();
      transaction_descriptor* descriptor = trans_descriptor_pool.New();
      current_transaction = descriptor;
      auto x = f();
      current_transaction = nullptr;
      bool passed = x.has_value();
      if (passed && descriptor->write_ptr_log.size() > 0) {
        // if transactions has stores and an old version was read, then abort
        if (abort_if_update) {
          passed = false;
          abort_type = FailedSpeculative;
        }
        else { // otherwise acquire locks in sorted order, and commit
          std::sort(descriptor->lock_log.data(),
                     descriptor->lock_log.data() + descriptor->lock_log.size());
          bool use_help = (try_count % 100) == 99;
          // abort if writes don't validate -- commented out, does not seem to help
          // for (int i = 0; i < descriptor->write_ptr_log.size(); i++) {
          //   auto [location, old_value, value] = descriptor->write_ptr_log[i];
          //   if (!simple_validate(location->load(), start_stamp)) passed = false;
          // }
          // if (passed) {
          passed = run_with_locks_rec(0, descriptor, [=,&abort_type] {
            abort_type = FailedValidate;
            commit(descriptor); return true;}, use_help);
          passed = passed && (descriptor->time_stamp.load() != failed);
          //}
        } 
      } 
      assert(current_transaction == nullptr);
      if (passed) {
        trans_descriptor_pool.Retire(descriptor);
        return x;
      } else {
        //if (abort_type == FailedLock)
        num_aborts++;
        auto& retired_log = descriptor->retired_log;

        // undo the logged retires
        for (int i=0; i < retired_log.size(); i++)
          epoch::undo_Retire(retired_log[i]);

        // undo logged allocates
        auto& allocated_log = descriptor->allocated_log;
        for (int i=0; i < allocated_log.size(); i++)
          epoch::undo_Allocate(allocated_log[i]);

        max_delay = 12000; 
        if (abort_type != FailedLock) { 
          if (delay == 0) {
            delay = 30 * descriptor->lock_log.size();
          } else {
            delay = std::min((int) std::ceil(1.4 * delay), max_delay);
          }
          float rnd = ((float) (parlay::hash64((unsigned long) descriptor) & 255)) / 256;
          int rand_delay = (int) ((1 + rnd) * delay);
          for (volatile int i=0; i < rand_delay; i++);
        } else for (volatile int i=0; i < 50; i++);
        
        if (try_count++ > 100000000000l/max_delay) {
          std::cout << "looks like an infinite retry loop" << std::endl;
          abort();
        }

        trans_descriptor_pool.Retire(descriptor);
        using RT = decltype(x);
        return RT();
      }});
    if (r.has_value()) return *r;
  }
}

  template <typename F>
  auto atomic_region(const F& f) {
    if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
      atomic_([&] {f(); return std::optional(true);});
  } else return atomic_(f); 
  }

  template <typename F>
  auto atomic_read_only(const F& f) {
    return verlib::with_snapshot([&] {return f();}); }


// template <typename F>
// auto do_now(const F& f) {
//   //transaction_descriptor* tmp = current_transaction;
//   //current_transaction = nullptr;
//   auto x = f();
//   //current_transaction = tmp;
//   return x;
// }

} // namespace tlf_internal

#endif // TLF_ATOMIC_H_
