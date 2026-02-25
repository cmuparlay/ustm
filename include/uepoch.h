// ***************************
// micro-epoch  (Author: Guy Blelloch)
// epoch based delayed reclamation 
// the interface consists of three functions
//   template <typename F>
//   uepoch::protect(const F& f) : takes a lambda or function f
//      with no arguments and with or without a result, and runs it in
//      protected mode
//   uepoch::delay(std::function<void()> f) : takes a function f and
//      delays running it until everyone who was in a protect at
//      the time of this call has now exited
//   uepoch::clear() : forces all delayed functions to run. Not safe to 
//      run concurrently with the other two functions
// protect can be safely nested (all but the outermost is ignored)
//
// Example:
//     uepoch::delay([&] {delete y;});
//     auto r = uepoch::protect([&] { ... x = y->left; ... return z;});
// If the delay is called while the the lambda passed to protect is
// running, the deletion of y is delayed until the lambda returns.
//
// It also supports adding functions to run just before and after an
// epoch is incremented using add_{before,after}_epoch_hook
//***************************

#ifndef UEPOCH_H_
#define UEPOCH_H_

#include <array>
#include <atomic>
#include <functional>
#include <iostream>
#include <utility>
#include <vector>
#include <type_traits>
#define UEPOCH

namespace uepoch {

  constexpr int max_threads = 4096; // can be increased
  constexpr long bytes_per_thread = 1ul << 26;

  // Keeps an array of flags to indicate what thread ids are
  // available (0 = available), and max id that has been used.
  struct thread_id_registry {
    std::array<std::atomic<long>,max_threads> flags;
    std::atomic<long> max_seen = 0;
    thread_id_registry() { for (auto& x : flags) x = 0; }
  };
    
  extern inline thread_id_registry& get_thread_id_registry() {
    static thread_id_registry ti;
    return ti;
  }

  // This is used as a thread local and keeps the thread's id.  Its
  // constructor finds a free id from the registry marking it as used,
  // and its destructor marks the id as unused.
  struct registered_id {
    long id;
    registered_id() {
      auto& ti = get_thread_id_registry();
      for (int i = 0; i < max_threads; i++) {
        long x = 0;
        if (ti.flags[i].load() == 0 &&
            ti.flags[i].compare_exchange_strong(x, 1)) {
          id = i;
          auto max = ti.max_seen.load();
          while (i > max && !ti.max_seen.compare_exchange_strong(max,i));
          return;
        }
      }
      std::cout << "thread registry is full, max threads is " << max_threads << std::endl;
      abort();
    }
    ~registered_id() { get_thread_id_registry().flags[id] = 0;}
  };

  long num_threads() {
    return get_thread_id_registry().max_seen.load(); }

  extern inline int thread_id() {
    static thread_local registered_id id_s;
    return id_s.id; }
  
  namespace internal {
    using thunk = std::function<void()>;

    // the main structure
    struct alignas(64) epoch_state {
      // announcements for each thread
      struct alignas(64) announce {
        std::atomic<long> last;
        announce() : last(-1l) {}};

      // the various delayed pools for each thread
      struct alignas(256) thread_state {
        std::vector<thunk> current; // delayed functions from current epoch
        std::vector<thunk> old;  // delayed functions from previous epoch
        long epoch = 0; // epoch on last delay
        long count = 0; // number of calls to delay since last epoch update
        thread_state() {}
      };

      // the full state
      std::atomic<long> current_epoch{0};
      std::array<announce, max_threads> announcements;
      std::array<thread_state, max_threads> pools;
      std::vector<thunk> before_epoch_hooks;
      std::vector<thunk> after_epoch_hooks;
  
      epoch_state() {}
      ~epoch_state() {clear();}

      // adds current epoch to the announcement slot of this thread
      // returns false if it was already announced
      bool announce() {
        size_t id = thread_id();
        if (announcements[id].last.load() == -1l) {
          announcements[id].last = current_epoch.load();
          return true;
        } else return false;
      }

      void unannounce() {
        size_t id = thread_id();
        announcements[id].last.store(-1l, std::memory_order_relaxed); }

      // run every once in a while to check if epoch can be incremented
      void update_epoch() {
        long current_e = current_epoch.load();
        int threads;
        while (true) { // unlikely to loop more than once
          threads = num_threads();
          if (threads > max_threads) {
            std::cerr << "epoch: too many threads" << std::endl;
            abort(); }
          // check if all announced epochs are at least current epoch
          for (int i=0; i < threads; i++)
            if ((announcements[i].last != -1l) &&
                announcements[i].last < current_e) return;
          if (num_threads() == threads) break;
        }
        for (const auto& h : before_epoch_hooks) h();
        if (current_epoch.compare_exchange_strong(current_e, current_e+1))
          for (const auto& h : after_epoch_hooks) h();
      }

      // runs all thunks in the pool and then empties it
      void clear_pool(std::vector<thunk>& v) {
        for (auto& x : v) x();
        v.clear();
      }
      
      void advance_epoch(int i) {
        thread_state& pool = pools[i];
        if (pool.epoch + 1 < current_epoch.load()) {
          // could call all the delayed functions in old at this point, but
          // we delay and put them in safe so they can be applied over time
          clear_pool(pool.old);
          std::swap(pool.old, pool.current);  // current is now empty
          pool.epoch = current_epoch.load();
        }
        long heuristic_threshold = 10 * num_threads();
        if (++pool.count == heuristic_threshold) { 
          pool.count = 0;
          update_epoch();
        }
      }

    public:
      // clears all the pools and returns number of entries cleared
      long clear() {
        update_epoch();
        long cnt = 0;
        for (long i = 0; i < max_threads; i++) {
          auto& p = pools[i];
          cnt += (p.old.size() + p.current.size());
          clear_pool(p.old);
          clear_pool(p.current);
        }
        return cnt;
      }

      // Delays running the function f until there are no longer any
      // announced threads active at the time of the call.
      void delay(thunk&& f) {
        auto i = thread_id();
        advance_epoch(i);
        pools[i].current.push_back(std::forward<thunk>(f));
      }
    }; // end struct epoch_state
  
  } // end namespace internal

  // Creates an epoch state on first call by a thread and then uses it
  // on future calls
  extern inline internal::epoch_state& get_epoch() {
    static internal::epoch_state epoch;
    return epoch;
  }

  // Ensures any delayed functions called by other threads while
  // running the function f are not executed until after f finishes.
  // The function f can either return a value or void.
  template <typename Thunk>
  auto protect(const Thunk& f) {
    auto& epoch = get_epoch();
    auto not_in_epoch = epoch.announce();
    if constexpr (std::is_void_v<std::invoke_result_t<Thunk>>) {
      f();
      if (not_in_epoch) epoch.unannounce();
    } else {
      auto v = f();
      if (not_in_epoch) epoch.unannounce();
      return v;
    }
  }

  void delay(std::function<void()>&& f) {
    get_epoch().delay(std::forward<std::function<void()>>(f)); }

  void add_before_epoch_hook(std::function<void()> f) {
    get_epoch().before_epoch_hooks.push_back(f); }

  void add_after_epoch_hook(std::function<void()> f) {
    get_epoch().after_epoch_hooks.push_back(f); }

  long clear() {return get_epoch().clear();}
} // end namespace uepoch

#endif // UEPOCH_H_
