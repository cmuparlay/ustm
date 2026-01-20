#include "fuse/fuse.h"
#include "list.h"
#include "parlay/parallel.h"
#include <iostream>

int main() {
  int n = 1000;
  ordered_list<long> a;

  // insert [0..n) into a in parallel
  parlay::parallel_for(0, n, [&] (long i) {
    fuse::atomic_region([&] { a.insert(i); });});

  if (a.size() != n) {
    std::cout << "Yikes: bad insert" << std::endl;
    abort();
  }

  // check all keys are properly found
  parlay::parallel_for(0, n, [&] (long i) {
    fuse::atomic_region([&] {
      if (!a.find(i)) {
        std::cout << "Yikes bad find" << std::endl;
        abort();
      }});});

  // remove [0..n) from a in parallel
  parlay::parallel_for(0, n, [&] (long i) {
    fuse::atomic_region([&] { a.remove(i); });});

  if (a.size() != 0) {
    std::cout << "Yikes: bad remove" << std::endl;
    abort();
  }

  std::cout << "I'm good" << std::endl;
}
