#include <array>
#include <verlib/verlib.h>
#include "set.h"
#include "test_sets.h"
#include "parse_command_line.h"

using K = unsigned long;
using V = std::array<unsigned long, 1>;

// if use this definition then enries are 100 bytes (8 + 23*4)
//using V = std::array<unsigned int, 23>;

int main(int argc, char* argv[]) {
  //#ifdef TINYSTM
  //  tiny::global_init tiny_global_init;
  //#endif
  commandLine P(argc,argv,R"(
[-n <size>]  : size of the map
[-r <rounds>]  : number of rounds to run the test
[-p <num-threads>] : number of threads for tests
[-z <zipfian-parameter>] : real parameter in range [0:1), 0 is uniform, .99 is highly skewed
[-u <update-percent>] : percent of operations that are updates (1/2 inserts, 1/2 deletes)
[-mfind <multifind-percent>] : percent of operations that are atomic multifinds
[-range <range-query-percent>] : percent of operations that are range queries
[-rs <range-query-size>] : size of the range query or multifind 
[-upsert] : do upserts instead of inserts and deletes
[-t <time>] : time to run each test in seconds
[-dense] : use keys in range [0:n)
[-simple_test] : sanity check
[-insert_find_delete] : just insert, then find, then delete
[-verbose] : print some info
[-stats] : print some memory statistics 
[-no_check] : do not check correctness of answers
[-rqthreads <num range query threads>] : number of dedicated range-query threads
[-trans <ops-per-transaction>] : size of transaction in operations (0 means no transaction)
[-latency <latency-cutoff-in-usecs>] : measure latency instead of throughput
)");

#ifdef HASH
  struct IntHash {
    std::size_t operator()(K const& k) const noexcept {
      return k * UINT64_C(0xbf58476d1ce4e5b9);}
  };
  using SetType = unordered_map<K,V,IntHash>;
#else
  using SetType = ordered_map<K,V>;
#endif
  
  size_t default_size = 100000;

  test_sets<SetType>(default_size, P);
}
