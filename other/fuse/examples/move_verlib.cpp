// This version uses the verlib interface
// The main differences from fuse are:
//  -- uses verlib namespace
//  -- must use find_locked instead of find in a transaction
//  -- does not support tlf_atomic or atomic and instead
//      supports versioned_ptr<T> and atomic_bool.
// A versioned_ptr<T> supports the same interface as fuse::atomic<T*>
// but the type T must inheret verlib::versioned.

// If this file is compiled without transactions, the first and second
// check should pass since each individual operation should linearize.
// However the third will likely not since the two finds can linearize
// separately.

#include "verlib/verlib.h"
#include "structures/hash_block/unordered_map.h"
#include "parlay/parallel.h"
#include <iostream>

int main() {
  int n = 1000000;

  verlib::hash_block<int,int> a(n);
  verlib::hash_block<int,int> b(n);
  verlib::hash_block<int,int> bad_locations(n);

  // insert [0..n) into a in parallel
  parlay::parallel_for(0, n, [&] (long i) {
    verlib::atomic_region([&] { a.insert(i, i); });});
  
  if (a.size() != n)
    std::cout << "Yikes: bad insert" << std::endl;

  // First n iterations move [0..n) from a to b in parallel.
  // Second n interations check that only in one at a time
  // If operations are not atomic then it could be in both
  // since we insert to b then remove from a.
  parlay::parallel_for(0, 2 * n, [&] (long i) {
    if (i < n) {                               
      verlib::atomic_region([&] {
        auto x = a.find_locked(i);
        if (x.has_value()) {
          b.insert(i, *x);
          a.remove(i);
        }});
    } else {
      verlib::atomic_region([&] {
        bool x = a.find_locked(i-n).has_value();
        bool y = b.find_locked(i-n).has_value();
        if (x == y) {
          bad_locations.insert(i, i);
        }
      });
    }
  });

  if (a.size() != 0 || b.size() != n)
    std::cout << "Yikes: bad move" << std::endl;

  if (bad_locations.size() == 0) std::cout << "I'm good" << std::endl;
  else std::cout << "Yikes!, failed atomicity: " << bad_locations.size() << std::endl;
}
