#include <atomic>
#include <vector>
#include <limits>

#include "parlay/alloc.h"
#include "parlay/primitives.h"

#ifndef PARLAY_EPOCH_H_
#define PARLAY_EPOCH_H_

#ifndef NDEBUG
// Checks for corruption of bytes before and after allocated structures, as well as double frees.
// Requires some extra memory to pad the front and back of a structure.
#define EpochMemCheck 1
#endif

// Can use malloc instead of parlay::type_allocator
#define USE_MALLOC 1

//#define USE_STEPPING

// If defined allows with_epoch to be nested (only outermost will set the epoch).
// Incurs slight overhead due to extra test, but allows wrapping a with_epoch
// around multiple operations which each do a with_epoch.
#define NestedEpochs 1

// Supports before_epoch_hooks and after_epoch_hooks, which are thunks
// that get run just before incrementing the epoch number and just after.
// The user set them with:
//    flck::internal::epoch.before_epoch_hooks.push_back(<mythunk>);
//    flck::internal::epoch.after_epoch_hooks.push_back(<myotherthunk>);

// ***************************
// epoch structure
// ***************************

namespace epoch {
namespace internal {
  constexpr int max_num_workers = 1024;

  // 
  template <typename F>
  void parallel_for(long i, F f) { parlay::parallel_for(0, i, f); }
  inline int worker_id() { return parlay::my_thread_id(); }
  inline int num_workers() { return parlay::num_thread_ids(); }

struct alignas(64) epoch_s {

  // functions to run when epoch is incremented
  std::vector<std::function<void()>> before_epoch_hooks;
  std::vector<std::function<void()>> after_epoch_hooks;

  struct alignas(64) announce_slot {
    std::atomic<long> last;
    announce_slot() : last(-1l) {}
  };

  std::vector<announce_slot> announcements;
  std::atomic<long> current_epoch;
  epoch_s() :
    announcements(std::vector<announce_slot>(max_num_workers)),
    current_epoch(0),
    epoch_state(0) {}

  long get_current() {
    return current_epoch.load();
  }

  long get_my_epoch() {
    return announcements[worker_id()].last;
  }

  void set_my_epoch(long e) {
    announcements[worker_id()].last = e;
  }

  std::pair<bool,int> announce() {
    size_t id = worker_id();
    assert (id < max_num_workers);
#ifdef NestedEpochs
    // by the time it is announced current_e could be out of date, but that should be OK
    if (announcements[id].last.load() == -1) {
      announcements[id].last = get_current();
      return std::pair(true, id);
    } else {
      return std::pair(false, id);
    }
#else
    long tmp = get_current();
    // apparently an exchange is faster than a store (write and fence)
    announcements[id].last.exchange(tmp, std::memory_order_seq_cst);
    return std::pair(true, id);
#endif
  }

  void unannounce(size_t id) {
    announcements[id].last.store(-1l, std::memory_order_release);
  }

  // top 16 bits are used for the process id, and the bottom 48 for
  // the epoch number
  using state = size_t;
  std::atomic<state> epoch_state;

  // Attempts to takes num_steps checking the announcement array to
  // see that all slots are up-to-date with the current epoch.  Once
  // they are, the epoch is updated.  Designed to deamortize the cost
  // of sweeping the announcement array--every thread only does
  // constant work.
  state update_epoch_steps(state prev_state, int num_steps) {
    state current_state = epoch_state.load();
    if (prev_state != current_state)
      return current_state;
    size_t i = current_state >> 48;
    size_t current_e = ((1ul << 48) - 1) & current_state;
    size_t workers = num_workers();
    if (i == workers) {
      for (const auto h : before_epoch_hooks) h();
      long tmp = current_e;
      if (current_epoch.load() == current_e &&
	  current_epoch.compare_exchange_strong(tmp, current_e+1)) {
	for (const auto h : after_epoch_hooks) h();
      }
      state new_state = current_e + 1;
      epoch_state.compare_exchange_strong(current_state, new_state);
      return epoch_state.load();
    }
    size_t j;
    for (j = i ; j < i + num_steps && j < workers; j++)
      if ((announcements[j].last != -1l) && announcements[j].last < current_e)
	return current_state;
    state new_state = (j << 48 | current_e);
    if (epoch_state.compare_exchange_strong(current_state, new_state))
      return new_state;
    return current_state;
  }

  // this version does the full sweep
  void update_epoch() {
    long current_e = get_current();

    // check if everyone is done with earlier epochs
    int workers;
    do {
      workers = num_workers();
      if (workers > max_num_workers) {
	std::cerr << "number of threads: " << workers
		  << ", greater than max_num_threads: " << max_num_workers << std::endl;
	abort();
      }
      for (int i=0; i < workers; i++)
	if ((announcements[i].last != -1l) && announcements[i].last < current_e)
	  return;
    } while (num_workers() != workers); // this is unlikely to loop

    // if so then increment current epoch
    for (const auto h : before_epoch_hooks) h();
    if (current_epoch.compare_exchange_strong(current_e, current_e+1)) {
      for (const auto h : after_epoch_hooks) h();
    }
  }
};
  
  extern inline epoch_s& get_epoch() {
    static epoch_s epoch;
    return epoch;
  }

// ***************************
// epoch pools
// ***************************

struct Link {
  Link* next;
  bool skip;
  void* value;
};

#ifdef USE_MALLOC
  inline Link* allocate_link() {return (Link*) malloc(sizeof(Link));}
  inline void free_link(Link* x) {return free(x);}
#else
  inline Link* allocate_link() {return (Link*) malloc(sizeof(Link));}
  inline void free_link(Link* x) {return free(x);}
  // using list_allocator = typename parlay::type_allocator<Link>;
  // inline Link* allocate_link() {return list_allocator::alloc();}
  // inline void free_link(Link* x) {return list_allocator::free(x);}
#endif
  
  using namespace std::chrono;

template <typename xT>
struct alignas(64) memory_pool {
private:

  static constexpr double milliseconds_between_epoch_updates = 20.0;
  long update_threshold;
  using sys_time = time_point<std::chrono::system_clock>;

  // each thread keeps one of these
  struct alignas(256) old_current {
    void* mem_pool;  // linked list of freed items (avoids sending back to allocator)
    Link* old;  // linked list of retired items from previous epoch
    Link* current; // linked list of retired items from current epoch
    long epoch; // epoch on last retire, updated on a retire
    long count; // number of retires so far, reset on updating the epoch
    sys_time time; // time of last epoch update
    epoch_s::state e_state;
    old_current() : e_state(0), mem_pool(nullptr), old(nullptr), current(nullptr), epoch(0) {}
  };

  // only used for debugging (i.e. EpochMemCheck=1).
  struct paddedT {
    long pad;
    std::atomic<long> head;
    xT value;
    std::atomic<long> tail;
  };

  std::vector<old_current> pools;
  int workers;

  bool* add_to_current_list(void* p) {
    auto i = worker_id();
    auto &pid = pools[i];
    advance_epoch(i, pid);
    Link* lnk = allocate_link();
    lnk->next = pid.current;
    lnk->value = p;
    lnk->skip = false;
    pid.current = lnk;
    return &(lnk->skip);
  }

  // destructs and frees a linked list of objects 
  void clear_list(Link* ptr) {
    // abort();
    while (ptr != nullptr) {
      Link* tmp = ptr;
      ptr = ptr->next;
      if (!tmp->skip) {
#ifdef EpochMemCheck
        paddedT* x = pad_from_T((T*) tmp->value);
        if (x->head != 10 || x->tail != 10) {
          if (x->head == 55) std::cerr << "double free" << std::endl;
          else std::cerr << "corrupted head" << std::endl;
          if (x->tail != 10) std::cerr << "corrupted tail" << std::endl;
          assert(false);
        }
#endif
        Delete((T*) tmp->value);
      }
      free_link(tmp);
    }
  }

  // computes size of list
  long size_of(Link* ptr) {
    long sum = 0;
    while (ptr != nullptr) {
      Link* tmp = ptr;
      ptr = ptr->next;
      sum++;
    }
    return sum;
  }

#define EPOCH_DELAY 1
  void advance_epoch(int i, old_current& pid) {
    epoch_s& epoch = get_epoch();
    if (pid.epoch + EPOCH_DELAY < epoch.get_current()) {
      clear_list(pid.old);
      pid.old = pid.current;
      pid.current = nullptr;
      pid.epoch = epoch.get_current();
    }
    // a heuristic
    //auto now = std::chrono::system_clock::now();
#ifdef USE_STEPPING
    long update_threshold = 320/sizeof(xT) + 10;
#else
    long update_threshold = (320/sizeof(xT) + 1) * num_workers();
#endif
    if (++pid.count == update_threshold) { // ||
        //std::chrono::duration_cast<std::chrono::milliseconds>(now - pid.time).count() >
        //milliseconds_between_epoch_updates * (1 + ((float) i)/workers)) {
      pid.count = 0;
      // pid.time = now;
#ifdef USE_STEPPING
      pid.e_state = get_epoch().update_epoch_steps(pid.e_state, 32);
#else
      get_epoch().update_epoch();
#endif
    }
  }

#ifdef  EpochMemCheck
  using nodeT = paddedT;
#else
  using nodeT = xT;
#endif

#ifdef USE_MALLOC
  nodeT* allocate_node() {
    // if there is an item in the mem pool, pop it from the pool,
    // otherwise use malloc.
    auto i = worker_id();
    nodeT* ptr = (nodeT*) pools[i].mem_pool;
    if (ptr == nullptr) 
      return (nodeT*) malloc(sizeof(nodeT));
    pools[i].mem_pool = (void*) *((nodeT**) ptr);
    return ptr;
  }
    
  void free_node(nodeT* x) {
    // push node onto the mem_pool for the worker
    auto i = worker_id();
    *((nodeT**) x) = (nodeT*) pools[i].mem_pool;
    pools[i].mem_pool = (void*) x;
  }

  // clear any items from the mem pool (nxt points to the head)
  void clear_mem_pool(nodeT* nxt) {
    while (nxt != nullptr) {
      nodeT* nxt_nxt = *((nodeT**) nxt);
      free(nxt);
      nxt = nxt_nxt;
    }
  }
      
#else
  using Allocator = parlay::type_allocator<nodeT>;
  nodeT* allocate_node() { return Allocator::alloc();}
  void free_node(nodeT* x) { return Allocator::free(x);}
  void clear_mem_pool(nodeT* nxt) {}
#endif
  
public:
  using T = xT;
  
  memory_pool() {
    update_threshold = 2048;
    pools = std::vector<old_current>(max_num_workers);
    for (int i = 0; i < workers; i++) {
      pools[i].count = (10 * i) % update_threshold;
      pools[i].time = system_clock::now();
    }
  }

  memory_pool(const memory_pool&) = delete;
  ~memory_pool() { } // clear(); }

  // noop since epoch announce is used for the whole operation
  void acquire(T* p) { }
  
  paddedT* pad_from_T(T* p) {
     size_t offset = ((char*) &((paddedT*) p)->value) - ((char*) p);
     return (paddedT*) (((char*) p) - offset);
  }
  
  // destructs and frees the object immediately
  void Delete(T* p) {
     p->~T();
#ifdef EpochMemCheck
     paddedT* x = pad_from_T(p);
     x->head = 55;
     free_node(x);
#else
     free_node(p);
#endif
  }

  template <typename ... Args>
  T* New(Args... args) {
#ifdef EpochMemCheck
    paddedT* x = allocate_node();
    x->pad = x->head = x->tail = 10;
    T* newv = &x->value;
    new (newv) T(args...);
    assert(check_not_corrupted(newv));
#else
    T* newv = allocate_node();
    new (newv) T(args...);
#endif
    return newv;
  }

  bool check_not_corrupted(T* ptr) {
#ifdef EpochMemCheck
    paddedT* x = pad_from_T(ptr);
    if (x->pad != 10) std::cerr << "memory_pool: pad word corrupted" << std::endl;
    if (x->head != 10) std::cerr << "memory_pool: head word corrupted" << std::endl;
     if (x->tail != 10) std::cerr << "memory_pool: tail word corrupted" << std::endl;
    return (x->pad == 10 && x->head == 10 && x->tail == 10);
#endif
    return true;
  }
                           
  template <typename F, typename ... Args>
  // f is a function that initializes a new object before it is shared
  T* New_Init(const F& f, Args... args) {
    T* x = New(args...);
    f(x);
    return x;
  }

  // retire and return a pointer if want to undo the retire
  bool* Retire(T* p) {
    return add_to_current_list((void*) p);}
  
  // clears all the lists 
  // to be used on termination
  void clear() {
    get_epoch().update_epoch();
    parallel_for(pools.size(), [&] (long i) {
      clear_mem_pool((nodeT*) pools[i].mem_pool);
      clear_list(pools[i].old);
      clear_list(pools[i].current);
      pools[i].mem_pool = nullptr;
      pools[i].old = pools[i].current = nullptr;
    });
  }

  void reserve(size_t n) {
#ifndef USE_MALLOC
    Allocator::reserve(n);
#endif
  }

  void stats() {
    // get_epoch().print_announce();
    // get_epoch().clear_announce();
    //std::cout << "epoch number: " << get_epoch().get_current() << std::endl;
    //for (int i=0; i < pools.size(); i++) {
    //  std::cout << "pool[" << i << "] = " << size_of(pools[i].old) << ", " << size_of(pools[i].current) << std::endl;
    //}
#ifndef USE_MALLOC
    Allocator::print_stats();
#endif
  }

  void shuffle(size_t n) {}
    
};

template <typename T>
extern inline memory_pool<T>& get_pool() {
  static memory_pool<T> pool;
  return pool;
}

} // end namespace internal

#if defined(OTHERSTM) && !defined(USE_EPOCH)

template <typename Thunk>
auto with_epoch(Thunk f) {
  if constexpr (std::is_void_v<std::invoke_result_t<Thunk>>) {
    f();
  } else {
    return f();
  }
}

#else

template <typename Thunk>
auto with_epoch(Thunk f) {
  auto& epoch = internal::get_epoch();
  auto [not_in_epoch, id] = epoch.announce();
  if constexpr (std::is_void_v<std::invoke_result_t<Thunk>>) {
    f();
#ifdef NestedEpochs
    if (not_in_epoch) 
#endif
      epoch.unannounce(id);
  } else {
    auto v = f();
#ifdef NestedEpochs
    if (not_in_epoch) 
#endif
      epoch.unannounce(id);
    return v;
  }
}

#endif

  template <typename T>
  using memory_pool = internal::memory_pool<T>;

  template <typename T>
  struct memory_pool_ {
    template <typename ... Args>
    static T* New(Args... args) {
      return internal::get_pool<T>().New(std::forward<Args>(args)...);}
    static void Delete(T* p) {internal::get_pool<T>().Delete(p);}
    static bool* Retire(T* p) {return internal::get_pool<T>().Retire(p);}
    static void clear() {internal::get_pool<T>().clear();}
    static void stats() {internal::get_pool<T>().stats();}
  };

  // x should point to the skip field of a link
  inline void undo_Retire(bool* x) { *x = true;}
  inline void undo_Allocate(bool* x) { *x = false;}


} // end namespace epoch

#endif //PARLAY_EPOCH_H_

