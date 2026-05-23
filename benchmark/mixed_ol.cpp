// YCSB-style point-update benchmark for the optimistic-locking (verlib)
// data structures under other/fuse/include/structures. The CLI, key
// generation, and output format mirror mixed.cpp so the same
// run_experiment.py harness can drive both. The difference is that every
// operation is a single point update — there are no multi-operation
// transactions, matching how the flock/verlib benchmarks are typically
// run.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <parlay/io.h>
#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>
#include <parlay/sequence.h>

#include "parse_command_line.h"
#include "zipfian.h"

#include <verlib/verlib.h>
#include "set.h"
// Structure header (set.h) defines
//   ordered_map<K,V>   (or unordered_map<K,V,Hash> when HASH is set)
// Each must support
//   bool insert(K,V); bool remove(K); std::optional<V> find(K); long check();
//   bool insert_(K,V); bool remove_(K); std::optional<V> find_(K);  (bare variants)
// optionally
//   bool upsert(K,V);          if UPSERT is defined
//   void range_(F,K,K);        if Range_Search is defined

std::string command_string = R"(
[-n <sizes>]  : comma separated sizes of the map (default 1000)
[-p <num-threads>] : number of threads to use (default # of threads on machine)
[-r <rounds>]  : number of rounds to run the test (default 1)
[-rqp <percent>] : percent of threads to be used for range queries (default 0)
[-rs <range-sizes>] : comma separated range sizes in number of elements returned (default 100)
[-t <time>] : time to run each test in seconds (default 1)
[-trans <ops-per-transaction>] : ignored — only single-op operations are supported (must be 1)
[-u <update-percents>] : comma separated percents of operations that are updates (default 5)
[-up] : do upserts instead of inserts/removes
[-verbose] : print additional information when running
[-z <zipfian-parameters>] : comma separated parameters in range [0:1), 0 (default) uniform, .99 highly skewed
example: -n 10000,10000000 -u 0,5,50 -z 0,.99 -rs 100,1000 -rqp 50 -r 3
)";

using K = unsigned long;
using V = std::array<unsigned long, 1>;

struct IntHash {
  std::size_t operator()(K const& k) const noexcept {
    return k * UINT64_C(0xbf58476d1ce4e5b9);}
};

struct statistics {
  long total;
  long added_count;
  long update_count;
  long update_success_count;
  long query_count;
  long query_total_value;
  long range_count;
  long range_sum;
  statistics() :
    total(0), added_count(0),
    update_count(0), update_success_count(0),
    query_count(0), query_total_value(0),
    range_count(0), range_sum(0) {}
};

statistics adds(statistics s1, statistics s2) {
  statistics r;
  r.total = s1.total + s2.total;
  r.added_count = s1.added_count + s2.added_count;
  r.update_count = s1.update_count + s2.update_count;
  r.update_success_count = s1.update_success_count + s2.update_success_count;
  r.query_count = s1.query_count + s2.query_count;
  r.query_total_value = s1.query_total_value + s2.query_total_value;
  r.range_count = s1.range_count + s2.range_count;
  r.range_sum = s1.range_sum + s2.range_sum;
  return r;
}

template<typename SetType, typename K, typename F>
std::pair<double,double>
run_mixed_operations(const parlay::sequence<K>& all,
                     const parlay::sequence<K>& unique,
                     float zipfian_param,
                     int update_percent,
                     long range_size,
                     int warmup,
                     const F& endKey,
                     commandLine P) {
  long m = all.size();
  bool upsert = P.getOption("-up");
  long n = upsert ? unique.size() : unique.size()/2;
  long p = P.getOptionIntValue("-p", parlay::num_workers());
  long rounds = P.getOptionIntValue("-r", 1);
  double trial_time = P.getOptionDoubleValue("-t", 1.0);
  bool verbose = P.getOption("-verbose");
  int range_query_percent = P.getOptionIntValue("-rqp", 0);
  int range_query_threads = std::ceil((range_query_percent * p)/100.0);
  enum op_type : char {Find, Insert, Remove};
  bool do_check = true;
#ifndef UPSERT
  if (upsert) {
    std::cout << "This structure does not support upsert" << std::endl;
    std::abort();
  }
#endif

  auto op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
    auto h = parlay::hash64(m+i)%2000;
    if (h < 10*update_percent) return Insert;
    else if (h < 20*update_percent) return Remove;
    else return Find; });

  double throughput = 0;
  double bytes_pe = 0;

  for (int i = 0; i < rounds + warmup; i++) {
    auto *map_ptr = new SetType(n);
    auto &tr = *map_ptr;
    auto check = [&]() { return map_ptr->check(); };
    if (verbose) std::cout << "round " << i << std::endl;
    using V = typename SetType::V;
    V default_value;
    default_value[0] = 3;

    // insert n distinct elements
    parlay::parallel_for(0, n, [&] (size_t i) {
      bool x = tr.insert(unique[i], default_value);
      if (!x) {
        std::cout << "Error: key found when inserting unique keys" << std::endl;
        std::abort();
      }
    }, 1000, true);

    if (do_check && verbose)
      std::cout << "inserted" << std::endl;

    long initial_size = 0;
    if (do_check) {
      initial_size = check();
      if (initial_size != n) {
        std::cout << "bad initial size: " << initial_size << std::endl;
        abort();
      }
    }

    parlay::sequence<statistics> processor_stats(p);

    auto start = std::chrono::system_clock::now();
    auto get_time = [] () {return std::chrono::system_clock::now();};
    auto time_diff = [] (auto start_time) {
      auto current = std::chrono::system_clock::now();
      using nano = std::chrono::nanoseconds;
      return std::chrono::duration_cast<nano>(current - start_time).count();
    };

    std::atomic<bool> finished = false;

    parlay::parallel_for(0, p, [&] (size_t proc_id) {
      long start = proc_id * m/p;
      long end = (proc_id + 1) * m/p;
      long j = start;
      long jj = start;
      statistics local_stats;
      auto start_time = get_time();
      long count = 100;

      while (true) {
        if (count-- == 0 || finished) {
          double duration = time_diff(start_time);
          if (duration > 1000000000 * trial_time || finished) {
            finished = true;
            processor_stats[proc_id] = local_stats;
            return;
          }
          count = 100;
        }

        // Offset jj from j on wrap so the operation type and key indices
        // don't stay paired forever. Without this, keys at "Insert"
        // positions get inserted once and stay inserted (so subsequent
        // inserts always fail), collapsing the update success ratio.
        if (j >= end) j = start;
        if (jj >= end) jj = start + 1;

#ifdef Range_Search
        if (proc_id < range_query_threads) {
          local_stats.range_count++;
          K begin = all[j];
          K end_k = endKey(begin, range_size);
          local_stats.range_sum += verlib::with_snapshot([&] {
            long cnt = 0;
            auto addf = [&] (K k, V v) { cnt++; };
            tr.range_(addf, begin, end_k);
            return cnt;
          }, range_size > 1000);
          j += 1;
          jj += 1;
          local_stats.total += 1;
          continue;
        }
#endif

        if (op_types[jj] == Find) {
          local_stats.query_count++;
          auto val = verlib::atomic_read_only([&] {return tr.find_(all[j]);});
          if (val.has_value()) local_stats.query_total_value += val.value()[0];
#ifdef UPSERT
        } else if (upsert) {
          local_stats.update_count++;
          if (tr.upsert(all[j], default_value)) {
            local_stats.added_count++;
            local_stats.update_success_count++;
          }
#endif
        } else if (op_types[jj] == Insert) {
          local_stats.update_count++;
          if (tr.insert(all[j], default_value)) {
            local_stats.added_count++;
            local_stats.update_success_count++;
          }
        } else if (op_types[jj] == Remove) {
          local_stats.update_count++;
          if (tr.remove(all[j])) {
            local_stats.added_count--;
            local_stats.update_success_count++;
          }
        }
        j += 1;
        jj += 1;
        local_stats.total += 1;
      }
    }, 1);

    double duration = ((double) time_diff(start))/1000000000;

    if (duration > 1.1 * trial_time)
      std::cout << "long duration: " << duration << std::endl;

    statistics overall_stats = parlay::reduce(processor_stats, parlay::binary_op(adds, statistics()));

    auto ustr = upsert ? "upsert=" : "u=";

    if (i >= warmup) {
      size_t num_ops = overall_stats.total;
      throughput += num_ops / (duration * 1e6);
      // trans is always 1 here but emit it so the output schema matches mixed.cpp
      std::cout << std::setprecision(3)
                << P.commandName() << ","
                << ustr << update_percent << ","
                << "trans=1,"
                << "n=" << n << ","
                << "p=" << p << ","
                << "z=" << zipfian_param << ","
                << "pa=0,"
                << "be=" << bytes_pe << ",";
      if (range_query_threads > 0)
        std::cout << "rs=" << range_size << ","
                  << "rqp=" << range_query_percent << ","
                  << "rqmops=" << overall_stats.range_count / (duration * 1e6) << ",";
      std::cout << "mops=" << num_ops / (duration * 1e6)
                << std::endl;
    }

    if (do_check) {
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
           ((10 * range_size < n) && (range_ratio < .9 * range_size))))
        std::cout << "bad range sizes, average range size returned = "
                  << range_ratio << std::endl;
      if (!upsert && (success_ratio < .4 || success_ratio > .6) && n > 10000)
        std::cout << "bad query success ratio, should be .5 is "
                  << success_ratio << std::endl;
      if (!upsert && (update_success_ratio < .4 || update_success_ratio > .6) && n > 10000)
        std::cout << "bad update success ratio, should be .5 is "
                  << update_success_ratio << std::endl;

      if (initial_size + total_added != final_cnt) {
        std::cout << "bad size: initial size = " << initial_size
                  << ", added " << total_added
                  << ", final size = " << final_cnt
                  << std::endl;
      }
    }

    // remove all keys and delete the map
    parlay::parallel_for(0, unique.size(), [&] (size_t i) {
      tr.remove(unique[i]);
    }, 100);
    if (do_check && check() != 0) {
      std::cout << "not empty after all keys deleted" << std::endl;
      abort();
    }

    delete map_ptr;
  }
  return std::pair(throughput/rounds, bytes_pe);
}

double geometric_mean(const std::vector<double>& vals) {
  double product = 1;
  for (auto x : vals) product = product * x;
  return pow(product, 1.0 / vals.size());
}

parlay::sequence<parlay::chars> comma_separated_arg(commandLine P, std::string option,
                                                    std::string default_str) {
  char* str = P.getOptionValue(option);
  if (str != nullptr)
    return parlay::tokens(std::string(str), [] (char x) {return x == ',';});
  else return parlay::sequence<parlay::chars>{parlay::to_sequence(default_str)};
}

template<typename SetType, typename K, typename F>
void run_loops(const parlay::sequence<K>& all_keys,
               const parlay::sequence<K>& unique_keys,
               const parlay::sequence<int>& percents,
               const parlay::sequence<int>& transaction_sizes,
               const parlay::sequence<int>& range_sizes,
               std::vector<double>& throughputs,
               std::vector<double>& bytes,
               float zipfian_param, int warm, const F& endKey, const commandLine& P) {
  for (auto range_size : range_sizes) {
    for (auto update_percent : percents) {
      for (auto trans_size : transaction_sizes) {
        if (trans_size != 1) {
          std::cout << "skipping trans=" << trans_size
                    << " (ol benchmark only supports point updates)" << std::endl;
          continue;
        }
        auto [tr, be] = run_mixed_operations<SetType, K>(all_keys, unique_keys,
                                                         zipfian_param, update_percent,
                                                         range_size, warm, endKey, P);
        throughputs.push_back(tr);
        if (be > 0) bytes.push_back(be);
        warm = false;
      }
    }
  }
}

int main(int argc, char* argv[]) {
  commandLine P(argc, argv, command_string);

  bool do_warmup = !P.getOption("-nowarmup");
  int warmup = P.getOptionIntValue("-warmup", do_warmup ? 2 : 0);

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
  bool loop = (percents.size() * transaction_sizes.size() * zipfians.size() * sizes.size()) > 1;
  bool upsert = P.getOption("-up");

  std::vector<double> throughputs;
  std::vector<double> bytes;

  using K = unsigned long;
#ifdef HASH
  using SetType = unordered_map<K,V,IntHash>;
#else
  using SetType = ordered_map<K,V>;
#endif

  parlay::sequence<K> unique_keys;
  parlay::sequence<K> all_keys;

  for (auto n : sizes) {
    if (upsert) n = n/2;
    K max_key = std::numeric_limits<K>::max();
    auto endKey = [=] (const K& begin, long range_size) {
      K range_gap = (max_key/n)*range_size;
      K end = begin + range_gap;
      if (end < begin) return max_key;
      else return end; };
    for (auto zipfian_param : zipfians) {
      long unique_size = 2 * n;

      auto candidates = parlay::delayed_tabulate(1.2 * unique_size, [&] (size_t i) {
        return (K) parlay::hash64(i);});
      auto shuffled = parlay::random_shuffle(parlay::remove_duplicates(candidates));
      unique_keys = parlay::tabulate(unique_size, [&] (size_t i) {return shuffled[i];});

      long m = P.getOptionIntValue("-m", 5 * unique_size + 1000 * p);
      Zipfian z(unique_size, zipfian_param);
      all_keys = parlay::tabulate(m, [&] (int i) {
        if (zipfian_param != 0.0) return unique_keys[z(i)];
        else return unique_keys[parlay::hash64(i) % unique_size];});
      if (zipfian_param != 0.0)
        unique_keys = parlay::random_shuffle(unique_keys);
      run_loops<SetType, K>(all_keys, unique_keys,
                            percents, transaction_sizes, range_sizes, throughputs, bytes,
                            zipfian_param, warmup, endKey, P);
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
