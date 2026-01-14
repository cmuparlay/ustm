// Contains main timer loops
//
// run_mixed_operation:
// runs on given keys for a fixed amount of time specified by -tt with
// a mix of operations, specified as follows
//   -u <update percent>
//   -range <range query percent>
// any left over percent goes to finds.
//   -rs <size>
// specifies the size of the range query or multifind.
//   -mfind
// specifies the queries should be grouped as multi finds (not compatible with range)
// If
//   -range_query_threads <num threads>
// is specified then that percent of threads are dedicated to range
// queries and the rest use the mix above.
// Reports throughput.
//
// run_insert_find_remove:
// inserts, then finds, then removes the given keys
//

#include <algorithm>
#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <optional>
#include <type_traits>
#include <tuple>
#include <vector>

#include <parlay/parallel.h>
#include <parlay/primitives.h>
#include <parlay/random.h>

struct statistics {
  long total;
  long added_count; // inserted - deleted
  long range_count;
  long range_sum;
  long mfind_count;
  long retry_count;
  long update_count;
  long update_success_count;
  long query_count;
  long query_total_value;
  long within_latency;
  long transactions;
  long aborts;
  statistics() :
    total(0), added_count(0), range_count(0), range_sum(0), mfind_count(0), retry_count(0), 
    update_count(0), update_success_count(0),
    query_count(0), query_total_value(0), within_latency(0), transactions(0), aborts(0) {}
};

// for adding across processors
statistics adds(statistics s1, statistics s2) {
  statistics r;
  r.total = s1.total + s2.total;
  r.added_count = s1.added_count + s2.added_count;
  r.range_count = s1.range_count + s2.range_count;
  r.range_sum = s1.range_sum + s2.range_sum;
  r.mfind_count = s1.mfind_count + s2.mfind_count;
  r.retry_count = s1.retry_count + s2.retry_count;
  r.update_count = s1.update_count + s2.update_count;
  r.update_success_count = s1.update_success_count + s2.update_success_count;
  r.query_count = s1.query_count + s2.query_count;
  r.query_total_value = s1.query_total_value + s2.query_total_value;
  r.within_latency = s1.within_latency + s2.within_latency;
  r.transactions = s1.transactions + s2.transactions;
  r.aborts = s1.aborts + s2.aborts;
  return r;
}    

#if defined(AtomicSingletonReadOnly)
// safe to run single read only atomics with only epoch protection
template <typename F>
auto atomic_singleton_read_only(const F& f) {
  #ifdef UEPOCH
  return uepoch::with_epoch([&] {return f();});
#else
  return epoch::with_epoch([&] {return f();});
#endif
}
#else
template <typename F>
auto atomic_singleton_read_only(const F& f) {
  return verlib::atomic_read_only([&] {return f();});}
#endif

#if defined(AtomicSingleton)
// safe to run single operations with no wrapper
template <typename F>
auto atomic_singleton(const F& f) {
  if constexpr (std::is_void_v<std::invoke_result_t<F>>) { f(); }
  else { return f();}
}
#else
template <typename F>
auto atomic_singleton(const F& f) {
  if constexpr (std::is_void_v<std::invoke_result_t<F>>) {
    verlib::atomic_region(f);
  } else {
    return verlib::atomic_region([&] () -> std::optional<std::invoke_result_t<F>> {
	return std::optional(f());});
  }
}
#endif	

template<typename SetType, typename K>
double run_mixed_operations(parlay::sequence<K>& all,
                            const parlay::sequence<K>& unique,
                            float zipfian_param,
                            K max_key,
                            bool use_upsert,
                            int update_percent,
                            int trans_size,
                            bool warmup,
                            commandLine P) {

  int m = all.size();
  int nn = unique.size();
  int n = use_upsert ? nn : nn/2;

  int p = P.getOptionIntValue("-p", parlay::num_workers());  

  int rounds = P.getOptionIntValue("-r", 1);

  double trial_time = P.getOptionDoubleValue("-t", 1.0);

  // Mix of operations
  //int update_percent = P.getOptionIntValue("-u", 20); 
  int range_size = P.getOptionIntValue("-rs",16);
  int range_percent = P.getOptionIntValue("-range",0);
  int multifind = P.getOption("-mfind");
  int latency = P.getOptionIntValue("-latency", 0); // in microseconds
  
  std::ofstream csv;
  if (char *csv_path = P.getOptionValue("-csv")) {
    csv.open(csv_path, std::ios_base::app);
  }

#ifndef Latency
  if (latency > 0)
    std::cout << "not compiled for the -latency argument, ignoring" << std::endl;
#endif
  
  // mix of threads
  int range_query_threads = P.getOptionIntValue("-rqthreads", 0);
  
#ifdef Range_Search
  K range_gap = (max_key/n)*range_size;

#else
  if (range_percent > 0) {
    std::cout << "range search not implemented for this structure" << std::endl;
    return 0.0;
  }
#endif

  // number of hash buckets for hash tables
  int buckets = P.getOptionIntValue("-bu", n);

  // shuffles the memory pool to break sharing of cache lines
  // by neighboring list/tree nodes
  bool shuffle = P.getOption("-shuffle");

  // verbose
  bool verbose = P.getOption("-verbose");

  // check consistency, on by default
  bool do_check = ! P.getOption("-no_check");
#ifdef CRAPYSTM
  do_check = false;
#endif

  // print memory usage statistics
  bool stats = P.getOption("-stats");

  // clear the memory pool between rounds
  bool clear = P.getOption("-clear");

  enum op_type : char {Find, Insert, Remove, Range, MultiFind};
 
  // initially set to all finds
  float update_per = update_percent;
  parlay::sequence<op_type> op_types(m, Find);
  if (multifind) {
    float update_frac = update_per / 100.0;
    update_per = 100.0 * range_size * update_frac / (1 + (range_size - 1) * update_frac);
  }

  op_types = parlay::tabulate(m, [&] (size_t i) -> op_type {
      auto h = parlay::hash64(m+i)%2000;
      if (h < 10*update_per) return Insert; 
      else if (h < 20*update_per) return Remove;
      else if (multifind) return MultiFind;
      else if (h < 20*(update_percent + range_percent)) return Range;
      else return Find; });

  parlay::internal::timer t;

  if (shuffle) SetType::shuffle(n);

  double throughput = 0;
  
  for (int i = 0; i < rounds + warmup; i++) {
#ifdef CRAPYSTM
    auto *map_ptr = atomic_singleton([=]() { return new SetType(buckets); });
#else
    auto *map_ptr = new SetType(buckets);
#endif
    auto &tr = *map_ptr;
    long len;
    auto check = [&]() {
#ifdef CRAPYSTM
      return 0;
#else
      return tr.check();
#endif
    };
    if (do_check) {
      size_t len = check();
      if (len != 0) {
        std::cout << "BAD LENGTH = " << len << std::endl;
      }
    }

    if (verbose) std::cout << "round " << i << std::endl;
    size_t initial_size;
    using V = typename SetType::V;
							  
    V default_value;
    default_value[0] = 3;

    // insert n distinct elements
    parlay::parallel_for(0, n, [&] (size_t i) {
                                 int cnt = 0;
      atomic_singleton([&] {
	bool x = tr.insert(unique[i], default_value);
        if (!x) {
          std::cout << "Error: key found when inserting unique keys" << std::endl;
          std::abort();
        }
        if (cnt > 0) std::cout << "retry: " << i << std::endl;
        // if (!tr.find(unique[i]).has_value()) {
        //   std::cout << "Error: key not found after insertion, at iteration: " << i << std::endl;
        //   cnt++; //std::abort();
        // }
                });
    }, 1000, true);
    
    // if (!use_upsert) {
    //   parlay::parallel_for(0, n, [&] (size_t i) {
    //     			   //std::cout << unique[i] << std::endl;
    //     atomic_singleton([&] {
    //       tr.remove(unique[i]);});
    //   }, 10, true);
    // }

    if (verbose)
      std::cout << "initial size = " << check() << std::endl;
    
    if (do_check) {
      initial_size = check();
    }

    size_t mp = m/p;
    // set some number of threads to only do range queries
    parlay::parallel_for(0, range_query_threads, [&] (size_t i) {
        for(int j = i*mp; j < (i+1)*mp; j++) op_types[j] = Range;
      }, 1);
    
    parlay::sequence<statistics> processor_stats(p);

    std::atomic<bool> finish = false;
    auto range_buffers = parlay::tabulate(p, [&] (size_t i) {
           return std::vector<K>(100ul + 2*std::max(range_size, trans_size));});

    t.start();
    auto start = std::chrono::system_clock::now();
    auto get_time = [] () {return std::chrono::system_clock::now();};
    auto time_diff = [] (auto start_time) {
      auto current = std::chrono::system_clock::now();
      using nano = std::chrono::nanoseconds;
      return std::chrono::duration_cast<nano>(current - start_time).count();
    };
    
    // every once in a while check if time is over
    auto check_end = [&] (long& cnt, statistics& local_stats,
                          int proc_id, const auto& start_time) -> bool {
      if (cnt >= 100 || finish) {
        double duration = time_diff(start_time);
        if (duration > 1000000000 * trial_time || finish) {
          if (!finish) finish.store(true);
#ifdef TransStats
	  local_stats.transactions = tlf_internal::num_transactions - local_stats.transactions;
	  local_stats.aborts = tlf_internal::num_aborts - local_stats.aborts;
#endif
          processor_stats[proc_id] = local_stats;
          return true;
        }
        cnt = 0;
      }
      return false;
    };

    // Run a loop with just transactions
    auto run_transactions = [&] (long start, long end, long proc_id) {
      long cnt = 0;
      long j = start;
      long jj = start;
      statistics local_stats;
#ifdef TransStats
      local_stats.transactions = tlf_internal::num_transactions;
      local_stats.aborts = tlf_internal::num_aborts;
#endif
      auto start_time = get_time();
      auto start_op_time = start_time;

      while (true) {
        if (check_end(cnt, local_stats, proc_id, start_time)) return;

#ifdef Latency
        if (latency > 0) start_op_time = get_time();
#endif
        if (j + trans_size > end) j = start;
        if (jj + trans_size > end) jj = start + trans_size;

        // if transaction is size one then only need to wrap atomic region around each operation
        // also a find can be put in an read_only region
        if (trans_size == 1) {
          if (op_types[jj] == Find) {
            local_stats.query_count++;
            //auto val = verlib::atomic_read_only([&] {return tr.find_(all[j]);});
            auto val = atomic_singleton_read_only([&] {return tr.find_(all[j]);});
            if (val.has_value()) local_stats.query_total_value += val.value()[0];
#ifdef UPSERT
          } else if (use_upsert) {
            local_stats.update_count++;
            if (verlib::atomic_region([&] {return std::optional(tr.upsert(all[j], default_value));})) {
              local_stats.added_count++;
              local_stats.update_success_count++;
            }
#endif
          } else if (op_types[jj] == Insert) {
            local_stats.update_count++;
            if (verlib::atomic_region([&] {return std::optional(tr.insert(all[j], default_value));})) {
              local_stats.added_count++;
              local_stats.update_success_count++;
            }
          }
          else if (op_types[jj] == Remove) {
            local_stats.update_count++;
            if (verlib::atomic_region([&] {return std::optional(tr.remove(all[j]));})) {
              local_stats.added_count--;
              local_stats.update_success_count++;
            }
          }
        } else {
          // run trans_size operations atomically in a transaction
          auto [added, queries, found, update_success] = verlib::atomic_region([&] {
            int loc_added = 0;
            int loc_queries = 0;
            int loc_found = 0;
            int loc_update_success = 0;
            for (int k=0; k < trans_size; k++) {
              if (op_types[jj+k] == Find) {
                loc_queries += 1;
                auto val = tr.find_locked(all[j+k]);
                if (val.has_value()) loc_found += val.value()[0];
#ifdef UPSERT
              } else if (use_upsert) {
                if (tr.upsert_(all[j+k], default_value)) {loc_added++; loc_update_success++;}
#endif
              } else if (op_types[jj+k] == Insert) {
                if (tr.insert(all[j+k], default_value)) {loc_added++; loc_update_success++;}
              } else if (op_types[jj+k] == Remove) {
                if (tr.remove(all[j+k])) {loc_added--; loc_update_success++;}
              }
            }
            return std::optional{std::tuple{loc_added, loc_queries, loc_found, loc_update_success}};});
#ifdef Latency
          if (latency > 0)
            if (time_diff(start_op_time) < ((double) latency))
              within_latency++;
#endif
          local_stats.added_count += added;
          local_stats.query_count += queries;
          local_stats.query_total_value += found;
          local_stats.update_count += trans_size - queries;
          local_stats.update_success_count += update_success;
        }
        j += trans_size;
        jj += trans_size;
        cnt += trans_size;
        local_stats.total += trans_size;
      } 
    };

    // **********************
    // Run a loop with operation mix
    // **********************
    auto run_operations = [&] (long start, long end, long proc_id) {
      long cnt = 0;
      long j = start;
      long jj = start;
      statistics local_stats;
#ifdef TransStats
      local_stats.transactions = tlf_internal::num_transactions;
      local_stats.aborts = tlf_internal::num_aborts;
#endif
      std::vector<K>& range_keys = range_buffers[proc_id];
      auto start_time = get_time();
      auto start_op_time = start_time;
      long xxx = 0;

      while (true) {
        if (check_end(cnt, local_stats, proc_id, start_time)) 
          return;

#ifdef Latency
        if (latency > 0) start_op_time = get_time();
#endif
        if (op_types[jj] == Find) {
          local_stats.query_count++;
          auto val = atomic_singleton_read_only([&] {return tr.find_(all[j]);});
          if (val.has_value()) {
            local_stats.query_total_value += val.value()[0];
          }
#ifdef UPSERT
	} else if (use_upsert) {
	  local_stats.update_count++;
	  if (atomic_singleton([&] {return tr.upsert(all[j], default_value);})) {
	    local_stats.added_count++;
            local_stats.update_success_count++;
          }
#endif
        } else if (op_types[jj] == Insert) {
          local_stats.update_count++;
	  if (atomic_singleton([&] {return tr.insert(all[j], default_value);})) {
            local_stats.added_count++;
            local_stats.update_success_count++;
          }
        }
        else if (op_types[jj] == Remove) {
          local_stats.update_count++;
          if (atomic_singleton([&] {return tr.remove(all[j]);})) {
            local_stats.added_count--;
            local_stats.update_success_count++;
          }
        }

#ifdef Range_Search
        else if (op_types[jj] == Range) {
          K end = ((all[j] > max_key - range_gap)
                   ? max_key : all[j] + range_gap);
          local_stats.range_count++;
          bool use_speculative = true; //(range_size < 2048);
          local_stats.range_sum += verlib::with_snapshot([&] {
            long cnt=0;
            auto addf = [&] (K k, V v) {
              assert(cnt < range_keys.size());
              range_keys[cnt++] = k;
            };
            tr.range_(addf, all[j], end);
#ifdef LazyStamp
            if (verlib::aborted) {local_stats.retry_count++;}
#endif
            return cnt;}, use_speculative);
        }
#endif
        else { // multifind
          local_stats.mfind_count++;
          local_stats.query_total_value += verlib::with_snapshot([&] {
            long loc_found = 0;
            long loc = j;
            for (long k = 0; k < range_size; k++) {
              auto val = tr.find_(all[loc]);
              loc += 1;
              if (loc >= end) loc -= start; // wrap around if needed
              if(val.has_value()) loc_found += val.value()[0];
#ifdef LazyStamp
              if (verlib::aborted) {
                local_stats.retry_count++;
                return 0l;
              }
#endif
            }
            return loc_found;});
          j += range_size;
          if (j >= end) j = start;
          jj += range_size;
          if (jj >= end) jj = start+range_size;
          cnt += range_size;
          local_stats.total += range_size;
          continue;
        }
#ifdef Latency
        if (latency > 0)
          if (time_diff(start_op_time) < ((double) latency))
            within_latency++;
#endif
        if (++j >= end) j = start;
        if (++jj >= end) jj = start+7;
        cnt++;
        local_stats.total++;
      }
    };

    // loop across the processors
    parlay::parallel_for(0, p, [&] (size_t proc_id) {
              long start = proc_id * mp;
        long end = (proc_id + 1) * mp;
        if (trans_size > 0) 
          run_transactions(start,end,proc_id);
        else
          run_operations(start,end,proc_id);
    }, 1);

    double duration = t.stop();
    if (duration > 1.1 * trial_time)
      std::cout << "duration: " << duration << std::endl;
    
    statistics overall_stats = parlay::reduce(processor_stats, parlay::binary_op(adds, statistics()));

    if (i >= warmup) { // don't report zeroth round -- warmup
      if (finish && (duration < trial_time/4))
        std::cout << "warning out of samples, finished in "
                  << duration << " seconds" << std::endl;

      size_t num_ops = overall_stats.total;
      if (latency > 0) {
        std::cout << "percent within latency = " << (100.0*overall_stats.within_latency)/num_ops << std::endl;
      }

      double percent_aborts = (overall_stats.transactions == 0 ?
                               0.0 :
                               overall_stats.aborts * 100.0 / overall_stats.transactions);

      throughput += num_ops / (duration * 1e6);
      if (range_query_threads == 0) {
        if (csv) {
          csv << std::setprecision(4)
            << P.commandName() << ","
            << update_percent << ','
            << range_size << ','
            << trans_size << ','
            << n << ','
            << p << ','
            << zipfian_param << ','
            << percent_aborts << ','
            << num_ops / (duration * 1e6) << std::endl;
        }
        std::cout << std::setprecision(4)
                  << P.commandName() << ","
                  << update_percent << (use_upsert ? "%upsert," : "%update,")
                  << range_percent << "%range,"
                  << (multifind ? "mfind," : "find,") 
                  << "rs=" << range_size << ","
                  << "trans=" << trans_size << ","
                  << "n=" << n << ","
                  << "p=" << p << ","
                  << "z=" << zipfian_param << ","
#ifdef TransStats
		  << "%aborts=" << percent_aborts << ","
#endif
                  << num_ops / (duration * 1e6) << std::endl;
      } else {
        std::cout << std::setprecision(4)
                  << P.commandName() << ","
                  << update_percent << "%update,"
                  << range_percent << "%range,"
                  << "rs=" << range_size << ","
                  << "trans=" << trans_size << ","
                  << "n=" << n << ","
                  << "p=" << p << ","
                  << "rq_threads=" << range_query_threads << ","
                  << "z=" << zipfian_param << ","
                  << "rq_throughput=" << overall_stats.range_count / (duration * 1e6) << ","
                  << "update_throughput=" << overall_stats.update_count / (duration * 1e6) << std::endl;
      }

      if (do_check) {
        size_t final_cnt = check();
        long total_added = overall_stats.added_count;
        long total_updates = overall_stats.update_count;
        double update_successes = overall_stats.update_success_count;
        double update_success_ratio = update_successes / total_updates;
        long total_mfind = overall_stats.mfind_count;
        long total_queries = overall_stats.query_count + total_mfind * range_size;
        double total_found = overall_stats.query_total_value / default_value[0];
        double success_ratio = total_found / total_queries;
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

	if (use_upsert) {
	  if (success_ratio != 1)
	    std::cout << "bad query success ratio, should be 1 is "
		      << success_ratio << std::endl;
	  if (update_success_ratio != 0)
	    std::cout << "bad upsert success ratio, should be 0 is "
		      << update_success_ratio << std::endl;
	} else {
	  if ((success_ratio < .4 || success_ratio > .6) && n > 10000)
	    std::cout << "bad query success ratio, should be .5 is "
		      << success_ratio << std::endl;
	  if ((update_success_ratio < .4 || update_success_ratio > .6) && n > 10000)
	    std::cout << "bad update success ratio, should be .5 is "
		      << update_success_ratio << std::endl;
	}
        if (multifind && verbose) {
          long mfind_sum = overall_stats.mfind_count;
          long retry_sum = overall_stats.retry_count;
#ifdef LazyStamp
          std::cout << "retry percent = " << (double) 100 * retry_sum / mfind_sum
                    << std::endl;
#endif
        }
        if (range_percent > 0 || range_query_threads > 0) {
          long range_sum = overall_stats.range_sum;
          long retry_sum = overall_stats.retry_count;
          long num_queries = overall_stats.range_count;
          long av_range_size = ((float) range_sum) / num_queries;
          if (av_range_size > 1.05 * range_size || av_range_size < .95 * range_size)
            std::cout << "bad average range size: "
                      << av_range_size << std::endl;
          if (verbose)
            std::cout << "average range size = " << ((float) range_sum) / num_queries
#ifdef LazyStamp
                      << ", retry percent = " << (double) 100 * retry_sum / num_queries 
#endif
                      << std::endl;
        }

        if (initial_size + total_added != final_cnt) {
          std::cout << "bad size: initial size = " << initial_size 
                    << ", added " << total_added
                    << ", final size = " << final_cnt 
                    << std::endl;
        }
      }
      // if (stats) {
      //   descriptor_pool.stats();
      //   log_array_pool.stats();
      //   os.stats();
      // }
    }
#ifdef DeleteAll
      parlay::parallel_for(0, nn, [&] (size_t i) {
        atomic_singleton([&] {
          tr.remove(unique[i]);});
      });
#endif
      
#ifndef CRAPYSTM
    delete map_ptr;
#endif
    if (clear) SetType::clear();
    if (stats) SetType::stats();
  }
  return throughput/rounds;
  //SetType::clear();
}

template<typename SetType, typename K>
void run_insert_find_remove(const parlay::sequence<K>& all,
                            long unique_count,
                            commandLine P) {
  long m = all.size();
  bool clear = P.getOption("-clear");
  int rounds = P.getOptionIntValue("-r", 1);
  int buckets = P.getOptionIntValue("-bu", unique_count);
  bool verbose = P.getOption("-verbose");
  bool do_check = ! P.getOption("-no_check");
  bool stats = P.getOption("-stats");
#ifdef CRAPYSTM
  do_check = false;
#endif
  
  for (int i = 1; i < rounds+1; i++) {
#ifdef CRAPYSTM
    auto *map_ptr = atomic_singleton([=]() { return new SetType(buckets); });
#else
    auto *map_ptr = new SetType(buckets);
#endif
    auto& tr = *map_ptr;
    if (verbose) std::cout << "round " << i << std::endl;
    parlay::internal::timer t;

    // millions of operations per second
    auto mops = [=] (double time) -> float {return m / (time * 1e6);};
    long len = parlay::remove_duplicates(all).size();
    parlay::sequence<bool> flags(m);
    using V = typename SetType::V;
    V default_value;
    default_value[0] = 123;

    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
      flags[i] = atomic_singleton([&] { return tr.insert(all[i], default_value);});
    }, 1);

    std::cout << "insert," << m << "," << mops(t.stop()) << std::endl;

    auto check = [&]() { return tr.check();};

    if (do_check) {
      long total = parlay::count(flags, true);
      if (verbose) std::cout << "doing check" << std::endl;
      long lenc = check();
      if (verbose) std::cout << "done check" << std::endl;
      if (lenc != len || total != len) {
        std::cout << "incorrect size after insert: distinct keys inserted = "
                  << len << " succeeded = " << total << " found = " << lenc
                  << std::endl;
      }
    }
    if (stats) {
      //descriptor_pool.stats();
      //log_array_pool.stats();
      SetType::stats();
    }

    auto search_seq = parlay::random_shuffle(all, parlay::random(1));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
        auto x = atomic_singleton_read_only([&] {return tr.find(search_seq[i]);});
        if (!x.has_value()) {
          std::cout << "key not found, i = " << i << ", key = " << search_seq[i] << std::endl;
          abort();
        } else if (*x != default_value) {
          std::cout << "bad value " << (*x)[0] << " at key " << search_seq[i] << std::endl;
          abort();
        }
        });
    std::cout << "find," << m << "," << mops(t.stop()) << std::endl;

    auto delete_seq = parlay::random_shuffle(all, parlay::random(2));
    t.start();
    parlay::parallel_for(0, m, [&] (size_t i) {
                                 int cnt = 0;
      bool x = atomic_singleton([&] {
                                  cnt++;
                                  auto x = tr.remove(delete_seq[i]);
                                  return x; });
      if (false)
        atomic_singleton([&] {
               if (tr.find(delete_seq[i]).has_value())
                 std::cout << "found after delete. " << delete_seq[i] << ", " << cnt << std::endl;
                         });
      
    });

    std::cout << "remove," << m << "," << mops(t.stop()) << std::endl;

    if (do_check) {
      len = check();
      if (len != 0) {
        std::cout << "BAD LENGTH = " << len << std::endl;
        tr.print();
      } else if(verbose) {
        std::cout << "CHECK PASSED" << std::endl;
      }
    }
#ifndef CRAPYSTM
    delete map_ptr;
#endif
  }
  if (clear) {
    SetType::clear();
  }
  if (stats) {
    if (clear) std::cout << "the following should be zero if no memory leak" << std::endl;
    SetType::stats();
  }
}
