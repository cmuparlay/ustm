// ***************************
// micro-epoch  (Author: Guy Blelloch)
// epoch based delayed reclamation 
// the interface consists of three functions
//   template <typename F>
//   uepoch::protect(const F& f) : takes a lambda or function f
//      with no arguments and with or without a result, and runs it in
//      protected mode
//   uepoch::delay(std::function<void(void)> f) : takes a function f and
//      delays running it until everyone who was in a with_epoch at
//      the time of the delay has now exited
//   uepoch::clear() : forces all delayed functions to run. not safe to run
//      concurrently with the other two functions
// with_epoch can be safely nested (all but the outermost is ignored)
//
// Example:
//     uepoch::delay([&] {delete y;});
//     auto r = uepoch::with_epoch([&] { ... x = y->left; ... return z;});
// If the delay is called concurrently with the with_epoch the deletion
// of y is delayed until the with_epoch finishes.
// ***************************

#ifndef UEPOCH_H_
#define UEPOCH_H_

#include <atomic>
#include <forward_list>
#include <functional>
#define UEPOCH

namespace uepoch {
  
  namespace internal {
    constexpr int max_threads = 1024;

    extern inline std::atomic<int>& num_threads() {
      static std::atomic<int> num_threads;
      return num_threads;  }

    extern inline int thread_id() {
      static thread_local int id{num_threads().fetch_add(1)};
      return id; }

    // the main structure
    struct alignas(64) epoch_state {
      // functions to run when epoch is incremented
      std::vector<std::function<void()>> before_epoch_hooks;
      std::vector<std::function<void()>> after_epoch_hooks;

      // announcements for each thread
      struct alignas(64) announce {
        std::atomic<long> last;
        announce() : last(-1l) {}};

      // the various delayed lists for each thread
      struct alignas(256) thread_state {
        using list = std::forward_list<std::function<void(void)>>;
        list current; // delayed functions from current epoch
        list old;  // delayed functions from previous epoch
        list safe; // delayed functions that are safe to call
        long epoch = 0; // epoch on last delay
        long count = 0; // number of calls to delay since last epoch update
        thread_state() {}
      };

      // the full state
      std::atomic<long> current_epoch{0};
      announce announcements[max_threads];
      thread_state pools[max_threads];
  
      epoch_state() {}
      ~epoch_state() {clear();}

      // adds current epoch to the announcement slot of this thread
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
          for (int i=0; i < threads; i++)
            if ((announcements[i].last != -1l) &&
                announcements[i].last < current_e) return;
          if (num_threads() == threads) break;
        }
        for (const auto h : before_epoch_hooks) h();
        if (current_epoch.compare_exchange_strong(current_e, current_e+1)) {
          for (const auto h : after_epoch_hooks) h();
        }
      }

      void advance_epoch(int i) {
        thread_state& pid = pools[i];
        if (pid.epoch + 1 < current_epoch.load()) {
          pid.safe.splice_after(pid.safe.cbefore_begin(), pid.old);
          pid.old.swap(pid.current);
          pid.epoch = current_epoch.load();
        }
        long heuristic_threshold = 5 * num_threads();
        if (++pid.count == heuristic_threshold) { 
          pid.count = 0;
          update_epoch();
        }
      }

    public:
      long clear() {
        update_epoch();
        long cnt = 0;
        for (long i = 0; i < max_threads; i++) {
          for (auto& x : pools[i].old) {x(); cnt++;}
          for (auto& x : pools[i].current) {x(); cnt++;}
          for (auto& x : pools[i].safe) {x(); cnt++;}
          pools[i].old.clear(); pools[i].current.clear();
          pools[i].safe.clear();
        }
        return cnt;
      }

      void delay(std::function<void(void)> f) {
        auto i = thread_id();
        advance_epoch(i);
        pools[i].current.push_front(std::move(f));
        if (!pools[i].safe.empty()) {
          pools[i].safe.front()();
          pools[i].safe.pop_front();
        }
      }
    }; // end struct epoch_state
  
  } // end namespace internal

  extern inline internal::epoch_state& get_epoch() {
    static internal::epoch_state epoch;
    return epoch;
  }

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

  void delay(std::function<void(void)> f) {
    get_epoch().delay(std::move(f));
  }

  void clear() {get_epoch().clear();}

  void add_before_epoch_hook(std::function<void()> f) {
    get_epoch().before_epoch_hooks.push_back(f);}
  void add_after_epoch_hook(std::function<void()> f) {
    get_epoch().after_epoch_hooks.push_back(f);}
} // end namespace uepoch

#endif // UEPOCH_H_
