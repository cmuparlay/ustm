#ifndef TLF_TRANS_DESCRIPTOR_H_
#define TLF_TRANS_DESCRIPTOR_H_

#include <array>
#include <vector>
#include <tuple>
#include <limits>
#include <atomic>
#include <utility>

namespace tlf_internal {
  using namespace verlib;
  
  using atomic_val = size_t;
  //thread_local int transaction_round = 0;
  
  // structure for holding the various logs
  // has a default size and will expand if needed
  template <typename T, int InitLength=32>
  struct transaction_log {
  private:
    static constexpr int init_log_length = InitLength;
    std::array<T,init_log_length> vals;
    // parlay::sequence<T> extra_vals;
    std::vector<T> extra_vals;
    int cnt;
  public:
    int size() {return cnt;}
    int add(T v) {
      if (cnt < init_log_length) {vals[cnt++] = v; return cnt-1;}
      if (cnt == init_log_length) {
	extra_vals.reserve(2*init_log_length);
	for (int i=0; i < init_log_length; i++)
	  extra_vals.push_back(vals[i]);
      }
      extra_vals.push_back(v);
      return cnt++;
    }
    T* data() {
      return (cnt > init_log_length) ? extra_vals.data() : &vals[0];}
    void clear() { cnt = 0; extra_vals.clear();}
    T& operator [](int i) {
      return (cnt > init_log_length) ? extra_vals[i] : vals[i];}
    transaction_log() : cnt(0) {}
  };

  // Special timestamps
  const TS tbd = std::numeric_limits<TS>::max()/4;
  const TS tbs = std::numeric_limits<TS>::max()/4 - 1;
  const TS failed = std::numeric_limits<TS>::max()/4 - 2;
  bool is_validating_stamped(TS x) { return x >> 62;}
  bool is_validating(TS x) { return is_validating_stamped(x) || x == tbs;}
  TS set_validating(TS x) { return x ^ (1l << 62);}
  TS get_validating_timestamp(TS x) { return x ^ (1l << 62);}
  
  // needed for forward reference
  struct versioned;

  // structure that stores transaction information, including logs and state
  struct transaction_descriptor {
    // originally "tbd", then "tbs", then either a real timestamp or "failed"
    flck::atomic_aba_free<TS> time_stamp;
#ifdef LargeLog
    static constexpr int init_log_size = 40;  // 35
    static constexpr int init_validate_log_size = 140; // 70
#else
    static constexpr int init_log_size = 8;
    static constexpr int init_validate_log_size = 32;
#endif

    //flck::internal::descriptor lock_descriptor;
    std::atomic<bool> acquired;

    // the thread id the descriptor belongs to
    const int owner;
    const int round;  // for debugging

    transaction_log<std::tuple<flck::atomic<versioned*>*,versioned*,versioned*>,
		    init_log_size> write_ptr_log;
    transaction_log<std::tuple<flck::atomic<versioned*>*,versioned*>,
		    init_validate_log_size> validate_ptr_log;
    transaction_log<bool*,init_log_size> retired_log;
    transaction_log<bool*,init_log_size> allocated_log;
    transaction_log<flck::internal::lock*,init_log_size> lock_log;

    struct hash_filter {
      static constexpr int num_bits = 1024;
      static constexpr int high_mask = ((int) std::log2(num_bits/64)) - 1;
      unsigned long hash_bits[num_bits/64];
      hash_filter() {
	for (int i = 0; i < num_bits/64; i++)
	  hash_bits[i] = 0;
      }

      std::pair<int,unsigned long> hash(unsigned long v) {
	auto x = v * UINT64_C(0xbf58476d1ce4e5b9); // linear transform
	auto h = (x ^ (x >> 31));  // non-linear transform
	return std::pair((h >> 6) & high_mask, 1ul << (h & 63));
      }

      // returns true if bit found (can return false positive)
      bool find(unsigned long v) {
	auto [i, mask] = hash(v);
	return (hash_bits[i] & mask) != 0;
      }
     
      void insert(unsigned long v) {
	auto [i, mask] = hash(v);
	hash_bits[i] |= mask;
      }
    };

    hash_filter filter;
      
    std::optional<int> find_in_write_log(flck::atomic<versioned*>& v) {
      if (filter.find((unsigned long) &v)) 
	for (int i=0; i < write_ptr_log.size(); i++)
	  if (std::get<0>(write_ptr_log[i]) == &v) {
	    return std::optional<int>(i);
	  }
      return std::optional<int>();
    }

    void add_to_write_log(flck::atomic<versioned*>& v, versioned* old_ptr, versioned* new_ptr) {
      std::optional<int> lid = find_in_write_log(v);
      if (lid.has_value()) std::get<2>(write_ptr_log[*lid]) = new_ptr;
      else {
	int id = write_ptr_log.add(std::tuple(&v, old_ptr, new_ptr));
	filter.insert((unsigned long) &v);
      }
    }
    
    TS atomic_set_stamp(TS old_s, TS new_s) {
      assert(time_stamp.load() != tbd);
      if (time_stamp.load() == old_s) {
	TS old = old_s;
	time_stamp.cam(old, new_s);
      }
      return time_stamp.load();
    }

    // void atomic_set_current_stamp() {
    //   if (time_stamp.load() == tbs)
    //     atomic_set_stamp(global_stamp.get_write_stamp());
    // }

    //transaction_descriptor() : time_stamp(tbd), owner(epoch::internal::worker_id()), round(++transaction_round) {
    transaction_descriptor() : time_stamp(tbd), owner(epoch::internal::worker_id()), round(0) {
      std::atomic_thread_fence(std::memory_order_seq_cst);
    }
  };
  thread_local transaction_descriptor* current_transaction = nullptr;

#ifndef FlockHelp
  flck::internal::acquired_pool<transaction_descriptor> trans_descriptor_pool;
#else
  flck::internal::acquired_pool<transaction_descriptor> trans_descriptor_pool;
  //flck::memory_pool<transaction_descriptor> trans_descriptor_pool;
#endif

} // namespace tlf_internal

#endif // TLF_TRANS_DESCRIPTOR_H_
