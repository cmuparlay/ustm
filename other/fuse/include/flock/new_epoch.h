// ***************************
// Epoch-based memory reclamation
// Supports:
//     epoch::with_epoch(F f),
// which runs f within an epoch, as well as:
//     epoch::New<T>(args...)
//     epoch::Retire(T* a)   -- delays destruction and free
//     epoch::Delete(T* a)   -- destructs and frees immediately
// Retire delays destruction and free until no operation that was in a
// with_epoch at the time it was run is still within the with_epoch.
//
// All operations take constant time overhead (beyond the cost of the
// system malloc and free).
//
// Designed to work with C++ threads, or compatible threading
// libraries.  In particular it uses thread_local variables, and no two
// concurrent processes can share the same instance of the  variable.
//
// When NDEBUG is not set, the operations check for memory corruption
// of the bytes immediately before and after the object, and check
// for double retires/deletes.  Also:
//     epoch::check_ptr(T* a)
// will check that an object allocated using epoch::New(..) has not
// been corrupted.
//
// Supports undoing retires.  This can be useful in transactional
// system in which an operation aborts, and any retires done during
// the operations have to be undone.  In particular Retire returns a
// pointer to a boolean.  Running
//    epoch::undo_retire(bool* x)
// will undo the retire.  Must be run in same with_epoch as the retire
// was run, otherwise it is too late to undo.  If you don't want
// to undo retires, you can ignore this feature.
//
// New<T>, Retire and Delete use a shared pool for the retired lists,
// which, although not very large, is not cleared until program
// termination.  A private pool can be created with
//     epoch::memory_pool<T> a;
// which then supports a->New(args...), a->Retire(T*) and
// a->Delete(T*).  On destruction of "a", all elements of the retired
// lists will be destructed and freed.
//
// Achieves constant times overhead by incrementally taking steps.
// In particular every Retire takes at most a constant number of
// incremental steps towards updating the epoch and clearing the
// retired lists.
//
// Developed as part of parlay project at CMU, initially for flock then
// used for verlib, and parlayhash.
// Current dependence on parlay is just for parlay::my_thread_id() and
// parlay::num_thread_ids() which are from "parlay/thread_specific.h".
// ***************************

#include <atomic>
#include <cstdlib>
#include <functional>
#include <list>
#include <ostream>
#include <vector>
#include <type_traits>
#include <utility>
// Needed for parlay::my_thread_id of parlay::num_thread_ids
//#include "parlay/internal/threads/thread_specific.h"

#ifndef PARLAY_EPOCH_H_
#define PARLAY_EPOCH_H_

#ifndef NDEBUG
// Checks for corruption of bytes before and after allocated structures, as well as double frees.
// Requires some extra memory to pad the front and back of a structure.
#define EpochMemCheck 1
#endif
//#define EpochMemCheck 1

#define USE_STEPPING 1
#define USE_UNDO 1

//#define USE_PARLAY_ALLOC

#ifdef USE_PARLAY_ALLOC
#include "parlay/alloc.h"
#endif

// ***************************
// epoch structure
// ***************************

namespace epoch {

  namespace internal {

  inline int worker_id() {return parlay::my_thread_id(); }
  inline int num_workers() {return parlay::num_thread_ids();}
  constexpr int max_num_workers = 1024;

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

  int announce() {
    size_t id = worker_id();
    assert(id < max_num_workers);
    while (true) {
      long current_e = get_current();
      long tmp = current_e;
      // apparently an exchange is faster than a store (write and fence)
      announcements[id].last.exchange(tmp, std::memory_order_seq_cst);
      if (get_current() == current_e) return id;
    }
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

  // Juat one epoch structure shared by all
  extern inline epoch_s& get_epoch() {
    static epoch_s epoch;
    return epoch;
  }

// ***************************
// type specific memory pools
// ***************************

 struct Link {
    Link* next;
    bool skip;
    void* value;
  };

#ifndef USE_PARLAY_ALLOC
  inline Link* allocate_link() {return (Link*) malloc(sizeof(Link));}
  inline void free_link(Link* x) {return free(x);}
#else
  using list_allocator = typename parlay::type_allocator<Link>;
  inline Link* allocate_link() {return list_allocator::alloc();}
  inline void free_link(Link* x) {return list_allocator::free(x);}
#endif

    template <typename xT>
struct alignas(64) memory_pool {
private:

  static constexpr double milliseconds_between_epoch_updates = 20.0;
  long update_threshold;
  using sys_time = std::chrono::time_point<std::chrono::system_clock>;

  // each thread keeps one of these
  struct alignas(256) old_current {
    Link* old;  // linked list of retired items from previous epoch
    Link* current; // linked list of retired items from current epoch
    long epoch; // epoch on last retire, updated on a retire
    long count; // number of retires so far, reset on updating the epoch
    sys_time time; // time of last epoch update
    epoch_s::state e_state;
    old_current() : e_state(0), old(nullptr), current(nullptr), epoch(0) {}
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

  void advance_epoch(int i, old_current& pid) {
    epoch_s& epoch = get_epoch();
    if (pid.epoch + 1 < epoch.get_current()) {
      clear_list(pid.old);
      pid.old = pid.current;
      pid.current = nullptr;
      pid.epoch = epoch.get_current();
    }
    // a heuristic
    auto now = std::chrono::system_clock::now();
#ifdef USE_STEPPING
    long update_threshold = 20;
#else
    long update_threshold = 10 * num_workers();
#endif
    if (++pid.count == update_threshold  ||
        std::chrono::duration_cast<std::chrono::milliseconds>(now - pid.time).count() >
        milliseconds_between_epoch_updates * (1 + ((float) i)/workers)) {
      pid.count = 0;
      pid.time = now;
#ifdef USE_STEPPING
      pid.e_state = get_epoch().update_epoch_steps(pid.e_state, 64);
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
  nodeT* allocate_node() {return (nodeT*) malloc(sizeof(nodeT));}
  void free_node(nodeT* x) {return free(x);}
#else
  using Allocator = parlay::type_allocator<nodeT>;
  nodeT* allocate_node() { return Allocator::alloc();}
  void free_node(nodeT* x) { return Allocator::free(x);}
#endif
  
public:
  using T = xT;
  
  memory_pool() {
    update_threshold = 2048;
    pools = std::vector<old_current>(max_num_workers);
    for (int i = 0; i < workers; i++) {
      pools[i].count = parlay::hash64(i) % update_threshold;
      pools[i].time = std::chrono::system_clock::now();
    }
  }

  memory_pool(const memory_pool&) = delete;
  ~memory_pool() {} // clear(); }

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
    for (int i=0; i < pools.size(); i++) {
      clear_list(pools[i].old);
      clear_list(pools[i].current);
      pools[i].old = pools[i].current = nullptr;
    }
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
struct alignas(64) retire_pool {
private:

  struct list_entry {
    char data[sizeof(T)];
  };

  // each thread keeps one of these
  struct alignas(256) old_current {
    Link* old;  // linked list of retired items from previous epoch
    Link* current; // linked list of retired items from current epoch
    long epoch; // epoch on last retire, updated on a retire
    long retire_count; // number of retires so far, reset on updating the epoch
    epoch_s::state e_state;
    old_current() : e_state(0), epoch(0), retire_count(0) {}
  };

  std::vector<old_current> pools;

  // destructs entries on a list
  void clear_list(std::list<list_entry>& lst) {
    for (list_entry& x : lst)
      ((T*) (&(x.data)))->~T();
    lst.clear();
  }

  void advance_epoch(int i, old_current& pid) {
    if (pid.epoch + 1 < get_epoch().get_current()) {
      clear_list(pid.old);
      pid.old = std::move(pid.current);
      pid.epoch = get_epoch().get_current();
    }
#ifdef USE_STEPPING
    long update_threshold = 10;
#else
    long update_threshold = 10 * num_workers();
#endif
    if (++pid.retire_count == update_threshold) {
      pid.retire_count = 0;
#ifdef USE_STEPPING
      pid.e_state = get_epoch().update_epoch_steps(pid.e_state, 8);
#else
      get_epoch().update_epoch();
#endif
    }
  }

 public:
  retire_pool() {
    long workers = max_num_workers;
    pools = std::vector<old_current>(workers);
    for (int i = 0; i < workers; i++) 
      pools[i].retire_count = 0;
  }

  retire_pool(const retire_pool&) = delete;
  ~retire_pool() { clear(); }

  void Retire(T* p) {
    auto i = worker_id();
    auto &pid = pools[i];
    advance_epoch(i, pid);
    list_entry x;
    strncpy(x.data, (char*) p, sizeof(T));
    pid.current.push_back(x);
  }

  // Clears all the lists, to be used on termination, or could be use
  // at a quiescent point when noone is reading any retired items.
  void clear() {
    get_epoch().update_epoch();
    for (int i=0; i < num_workers(); i++) {
      clear_list(pools[i].old);
      clear_list(pools[i].current);
    }
  }

  void stats() {}
  void shuffle(size_t n) {}

};

} // namespace internal

  
// ***************************
// The public interface
// ***************************
  
  // x should point to the skip field of a link
  inline void undo_Retire(bool* x) { *x = true;}
  inline void undo_Allocate(bool* x) { *x = false;}
  
  template <typename T>
  using memory_pool = internal::memory_pool<T>;

  template <typename T>
  extern inline memory_pool<T>& get_default_pool() {
    static memory_pool<T> pool;
    return pool;
  }

  template <typename T>
  using retire_pool = internal::retire_pool<T>;

  template <typename T>
  extern inline retire_pool<T>& get_default_retire_pool() {
    static retire_pool<T> pool;
    return pool;
  }

  template <typename T, typename ... Args>
  static T* New(Args... args) {
    return get_default_pool<T>().New(std::forward<Args>(args)...);}

  template <typename T>
  static void Delete(T* p) {get_default_pool<T>().Delete(p);}

  template <typename T>
#ifdef USE_UNDO
  static bool* Retire(T* p) {return get_default_pool<T>().Retire(p);}
#else
  void Retire(T* p) {return get_default_pool<T>().Retire(p);}
#endif
    
  template <typename T>
  static bool check_ptr(T* p, bool silent=false) {
    return get_default_pool<T>().check_ptr(p, silent);}

  template <typename T>
  static void clear() {get_default_pool<T>().clear();}

  //template <typename T>
  //static void stats() {get_default_pool<T>().stats();}

  template <typename Thunk>
  auto with_epoch(Thunk f) {
    int id = internal::get_epoch().announce();
    if constexpr (std::is_void_v<std::invoke_result_t<Thunk>>) {
      f();
      internal::get_epoch().unannounce(id);
    } else {
      auto v = f();
      internal::get_epoch().unannounce(id);
      return v;
    }
  }

} // end namespace epoch

#endif //PARLAY_EPOCH_H_


