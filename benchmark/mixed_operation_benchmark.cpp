#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <tuple>
#include <vector>
#include "parse_command_line.h"
#include "zipfian.h"

#include <parlay/io.h>
#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>

#include "transactions.h"
// Requires
//   stm::atomic_region(F f)
//   stm::atomic_read_only(F f)
//   stm::atomic_read_only_singleton(F f)

#include "set.h"
// Requires
//   ordered_map<K,V>
// or if HASH is defined
//   unordered_map<K,V,Hash> 
// each must support:
//   bool insert(K k, V v);
//   bool remove(K k, V v);
//   std::optional<V> find(K k);
//   long check();   -- returns size
// and if Range_Search is defined:
//   range(Func f, K start, K end)

std::string command_string = R"(
[-n <sizes>]  : comma separated sizes of the map (default 1000)
[-p <num-threads>] : number of threads to use (default # of threads on machine)
[-r <rounds>]  : number of rounds to run the test (default 1)
[-rqp <percent>] : percent of threads to be used for range queries (default 0)
[-rs <range-sizes>] : comma separated range sizes in number of elements returned (default 100)
[-t <time>] : time to run each test in seconds (default 1)
[-trans <ops-per-transaction>] : comma separated sizes of transaction in operations (default 1)
[-u <update-percents>] : comma separated percents of operations that are updates (default 5)
[-verbose] : print additional information when running
[-z <zipfian-parameters>] : comma separated parameters in range [0:1), 0 (default) uniform, .99 highly skewed
example: -n 10000,10000000 -u 0,5,50 -z 0,.99 -trans 1,16 -rs 100,1000 -rqp 50 -r 3
)";

using K = unsigned long;
using V = std::array<unsigned long, 1>;

#ifdef HASH
  struct IntHash {
    std::size_t operator()(K const& k) const noexcept {
      return k * UINT64_C(0xbf58476d1ce4e5b9);}
  };
  using SetType = unordered_map<K,V,IntHash>;
#else
  using SetType = ordered_map<K,V>;
#endif

struct statistics {
  long total;
  long added_count; 
  long retry_count;
  long update_count;
  long update_success_count;
  long query_count;
  long query_total_value;
  long range_count;
  long range_sum;
  statistics() :
    total(0), added_count(0), retry_count(0), 
    update_count(0), update_success_count(0),
    query_count(0), query_total_value(0),
    range_count(0), range_sum(0)
  {}
};

// for adding across processors
statistics adds(statistics s1, statistics s2) {
  statistics r;
  r.total = s1.total + s2.total;
  r.added_count = s1.added_count + s2.added_count;
  r.retry_count = s1.retry_count + s2.retry_count;
  r.update_count = s1.update_count + s2.update_count;
  r.update_success_count = s1.update_success_count + s2.update_success_count;
  r.query_count = s1.query_count + s2.query_count;
  r.query_total_value = s1.query_total_value + s2.query_total_value;
  r.range_count = s1.range_count + s2.range_count;
  r.range_sum = s1.range_sum + s2.range_sum;
  return r;
}    

template<typename SetType, typename K>
double run_mixed_operations(parlay::sequence<K>& all,
                            const parlay::sequence<K>& unique,
                            float zipfian_param,
                            int update_percent,
                            int trans_size,
                            long range_size,
                            bool warmup,
                            commandLine P) {
  long m = all.size();
  // half the unique elements on average are in the table
  long n = unique.size()/2; 
  long p = P.getOptionIntValue("-p", parlay::num_workers());  
  long rounds = P.getOptionIntValue("-r", 1);
  double trial_time = P.getOptionDoubleValue("-t", 1.0);
  bool verbose = P.getOption("-verbose");
  int range_query_percent = P.getOptionIntValue("-rqp", 0);
  int range_query_threads = (range_query_percent * p)/100;
  enum op_type : char {Find, Insert, Remove};

  auto op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%2000;
      if (h < 10*update_percent) return Insert; 
      else if (h < 20*update_percent) return Remove;
      else return Find; });

  double throughput = 0;

  // loop over rounds
  for (int i = 0; i < rounds + warmup; i++) {
    auto *map_ptr = stm::atomic_region([=]() { return new SetType(n); });
    auto &tr = *map_ptr;
    auto check = [&]() {return tr.check();};
    if (verbose) std::cout << "round " << i << std::endl;
    using V = typename SetType::V;
    V default_value;
    default_value[0] = 3;
    
    // insert n distinct elements
    parlay::parallel_for(0, n, [&] (size_t i) {
      stm::atomic_region([&] {
	bool x = tr.insert(unique[i], default_value);
        if (!x) {
          std::cout << "Error: key found when inserting unique keys" << std::endl;
          std::abort();
        }});}, 1000, true);

    size_t initial_size = check();

    if (verbose)
      std::cout << "initial size = " << initial_size << std::endl;

    if (initial_size != n) {
      std::cout << "bad initial size: " << initial_size << std::endl;
      abort();
    }

    parlay::sequence<statistics> processor_stats(p);

    auto start = std::chrono::system_clock::now();
    auto get_time = [] () {return std::chrono::system_clock::now();};
    auto time_diff = [] (auto start_time) {
      auto current = std::chrono::system_clock::now();
      using nano = std::chrono::nanoseconds;
      return std::chrono::duration_cast<nano>(current - start_time).count();
    };

    K max_key = std::numeric_limits<K>::max();
    K range_gap = (max_key/n)*range_size;

    std::atomic<bool> finished = false;
    
    // loop across the processors
    parlay::parallel_for(0, p, [&] (size_t proc_id) {
      long start = proc_id * m/p;
      long end = (proc_id + 1) * m/p;
      long cnt = 0;
      // when we wrap around the following two indexes are offset 
      // this means we don't match the same operations with the same data
      long j = start; // index for all[j]
      long jj = start; // index for op_types[jj]
      statistics local_stats;
      auto start_time = get_time();

      while (true) {
        // check if time is up
        double duration = time_diff(start_time);
        if (finished || duration > 1000000000 * trial_time) {
          finished = true;
          processor_stats[proc_id] = local_stats;
          return;
        }
        // on wraparound set offsets are different
        if (j + trans_size > end) j = start;
        if (jj + trans_size > end) jj = start + trans_size;
#ifdef Range_Search
        if (proc_id < range_query_threads) {
          local_stats.range_count++;
          K begin = all[j];
          K end = begin + range_gap;
          if (end < begin) end = max_key;
          local_stats.range_sum += stm::atomic_read_only([&] {
            long cnt = 0;
            auto addf = [&] (K k, V v) { cnt++; };
            tr.range(addf, begin, end);
            return cnt;});
          j += trans_size;
          jj += trans_size;
          continue;
        }
#endif
        
        // if transaction is size one then only need to wrap atomic region around each operation
        // also a find can be put in an read_only region
        if (trans_size == 1) {
          if (op_types[jj] == Find) {
            local_stats.query_count++;
            auto val = stm::atomic_read_only_singleton([&] {return tr.find(all[j]);});
            if (val.has_value()) local_stats.query_total_value += val.value()[0];
          } else if (op_types[jj] == Insert) {
            local_stats.update_count++;
            if (stm::atomic_region([&] {return tr.insert(all[j], default_value);})) {
              local_stats.added_count++;
              local_stats.update_success_count++;
            }
          }
          else if (op_types[jj] == Remove) {
            local_stats.update_count++;
            if (stm::atomic_region([&] {return tr.remove(all[j]);})) {
              local_stats.added_count--;
              local_stats.update_success_count++;
            }
          }
        } else {
          // run trans_size operations atomically in a transaction
          auto [added, queries, found, update_success] = stm::atomic_region([&] {
            int loc_added = 0;
            int loc_queries = 0;
            int loc_found = 0;
            int loc_update_success = 0;
            for (int k=0; k < trans_size; k++) {
              if (op_types[jj+k] == Find) {
                loc_queries += 1;
                auto val = tr.find(all[j+k]);
                if (val.has_value()) loc_found += val.value()[0];
              } else if (op_types[jj+k] == Insert) {
                if (tr.insert(all[j+k], default_value)) {loc_added++; loc_update_success++;}
              } else if (op_types[jj+k] == Remove) {
                if (tr.remove(all[j+k])) {loc_added--; loc_update_success++;}
              }
            }
            return std::tuple{loc_added, loc_queries, loc_found, loc_update_success};});
          local_stats.added_count += added;
          local_stats.query_count += queries;
          local_stats.query_total_value += found;
          local_stats.update_count += trans_size - queries;
          local_stats.update_success_count += update_success;
        }
        j += trans_size;
        jj += trans_size;
        local_stats.total += trans_size;
      } 
    }, 1);

    double duration = time_diff(start)/1000000000;
    if (duration > 1.1 * trial_time)
      std::cout << "long duration: " << duration << std::endl;
    
    statistics overall_stats = parlay::reduce(processor_stats, parlay::binary_op(adds, statistics()));

    if (i >= warmup) { // don't report zeroth round -- warmup
      size_t num_ops = overall_stats.total;
      throughput += num_ops / (duration * 1e6);
      std::cout << std::setprecision(3)
                << P.commandName() << ","
                << "u=" << update_percent << ","
                << "trans=" << trans_size << ","
                << "n=" << n << ","
                << "p=" << p << ","
                << "z=" << zipfian_param << ",";
      if (range_query_threads > 0)
        std::cout << "rs=" << range_size << ","
                  << "rqp=" << range_query_percent << ","
                  << "rqmops=" << overall_stats.range_count / (duration * 1e6) << ",";
      std::cout << "mops=" << num_ops / (duration * 1e6)
                << std::endl;
    }

    // now we do various consistency checks
    size_t final_cnt = check();
    long total_added = overall_stats.added_count;
    long total_updates = overall_stats.update_count;
    double update_successes = overall_stats.update_success_count;
    double update_success_ratio = update_successes / total_updates;
    long total_queries = overall_stats.query_count;
    double total_found = overall_stats.query_total_value / default_value[0];
    double success_ratio = total_found / total_queries;
    int range_ratio = overall_stats.range_sum / (overall_stats.range_count + 1);
    if (verbose) {
      std::cout << "total updates =     " << total_updates 
                << "\nsuccesful updates = " << (long) update_successes
                << "\ntotal queries =     " << total_queries 
                << "\nsuccesful queries = " << (long) total_found
                << "\ninitial size =      " << initial_size
                << "\ntotal added =       " << total_added
                << "\nfinal size =        " << final_cnt 
                << std::endl;
    }

    if (range_query_percent > 0 &&
        (range_ratio > 1.1 * range_size ||
         (10 * range_size < n) && (range_ratio < .9 * range_size)))
      std::cout << "bad range sizes, average range size returned = "
                << range_ratio << std::endl;
    if ((success_ratio < .4 || success_ratio > .6) && n > 10000)
      std::cout << "bad query success ratio, should be .5 is "
                << success_ratio << std::endl;
    if ((update_success_ratio < .4 || update_success_ratio > .6) && n > 10000)
      std::cout << "bad update success ratio, should be .5 is "
                << update_success_ratio << std::endl;

    if (initial_size + total_added != final_cnt) {
      std::cout << "bad size: initial size = " << initial_size 
                << ", added " << total_added
                << ", final size = " << final_cnt 
                << std::endl;
    }

    // finally remove all the keys and delete the map
    parlay::parallel_for(0, unique.size(), [&] (size_t i) {
      stm::atomic_region([&] {
          tr.remove(unique[i]);});
      }, 100);
    stm::atomic_region([=] { delete map_ptr; });

#ifdef UEPOCH_H_
    uepoch::clear();
#endif
  }  return throughput/rounds;
}

double geometric_mean(const std::vector<double>& vals) {
  double product = 1;
  for (auto x : vals) product = product * x;
  return  pow(product, 1.0 / vals.size());
}

parlay::sequence<parlay::chars> comma_separated_arg(commandLine P, std::string option,
                                                    std::string default_str) {
    char* str = P.getOptionValue(option);
    if (str != nullptr) 
      return parlay::tokens(std::string(str), [] (char x) {return x == ',';});
    else return parlay::sequence<parlay::chars>{parlay::to_sequence(default_str)};
}

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,command_string);

  bool warmup = !P.getOption("-nowarmup");

  auto sizes = parlay::map(comma_separated_arg(P, "-n", "1000"),
                           parlay::chars_to_long);
  auto zipfians = parlay::map(comma_separated_arg(P, "-z", "0.0"),
                              parlay::chars_to_float);
  auto percents = parlay::map(comma_separated_arg(P, "-u", "5"),
                              parlay::chars_to_int);
  auto transaction_sizes = parlay::map(comma_separated_arg(P, "-trans", "1"),
                                       parlay::chars_to_int);
  auto range_sizes = parlay::map(comma_separated_arg(P, "-rs", "100"),
                                 parlay::chars_to_int);
  long p = P.getOptionIntValue("-p", parlay::num_workers());  

  using key_type = unsigned long;
  parlay::sequence<key_type> unique_keys;
  parlay::sequence<key_type> all_keys;
  key_type max_key = 0;

  bool loop = (percents.size() * transaction_sizes.size() * zipfians.size() * sizes.size()) > 1;
    
  std::vector<double> throughputs;
    
  for (auto n : sizes) {
    bool warm = warmup;
    for (auto zipfian_param : zipfians) {
      long unique_size = 2 * n;

      // first generate the shuffled unique keys
      auto candidates = parlay::delayed_tabulate(1.2 * unique_size, [&] (size_t i) {
                return (key_type) parlay::hash64(i);}); // generate 64-bit keys
      auto shuffled = parlay::random_shuffle(parlay::remove_duplicates(candidates));
      unique_keys = parlay::tabulate(unique_size, [&] (size_t i) {return shuffled[i];});

      // then put them into a sequence of length m
      long m = P.getOptionIntValue("-m", 10 * n + 1000 * p);
      Zipfian z(unique_size, zipfian_param);
      all_keys = parlay::tabulate(m, [&] (int i) {
                     if (zipfian_param != 0.0) return unique_keys[z(i)];
                     else return unique_keys[parlay::hash64(i) % unique_size];});
      if (zipfian_param != 0.0)
        unique_keys = parlay::random_shuffle(unique_keys);

      for (auto range_size : range_sizes) {
      for (auto update_percent : percents) {
        for (auto trans_size : transaction_sizes) {
          auto tr = run_mixed_operations<SetType>(all_keys, unique_keys,
                                                  zipfian_param, update_percent,
                                                  trans_size, range_size, warm, P);
          throughputs.push_back(tr);
          warm = false;
        }
      }
      }
    }
  }

  if (loop) {
    std::cout << std::setprecision(4) 
              << P.commandName() << ","
              << "GEOMETRIC MEAN OF THROUGHPUTS: "
              << geometric_mean(throughputs)
              << std::endl;
  }
}
