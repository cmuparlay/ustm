#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <limits>
#include <optional>
#include <string>
#include <type_traits>
#include <tuple>
#include <vector>
#include "parse_command_line.h"

#include <parlay/parallel.h>
#include <parlay/utilities.h>

#include "transactions.h"

std::string command_string = R"(
[-n <size>]  : 
[-p <num-threads>] : 
[-t <time>]  : 
)";

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,command_string);
  long p = P.getOptionIntValue("-p", parlay::num_workers());  
  long n = P.getOptionIntValue("-n", 1000);
  long trial_time = P.getOptionDoubleValue("-t", 1.0);

  std::vector<stm::atomic<long>> a(n);
  std::vector<long> counts(p, 0);
  for (long i = 0; i < a.size(); i++) a[i] = i;
  long expected = (n * (n - 1))/2;

  auto start = std::chrono::system_clock::now();
  auto get_time = [] () {return std::chrono::system_clock::now();};
  auto time_diff = [] (auto start_time) {
                     auto current = std::chrono::system_clock::now();
                     return std::chrono::duration_cast<std::chrono::seconds>(current - start_time).count();
                   };

  parlay::parallel_for(0, p, [&] (size_t i) {
    auto start_time = get_time();
    long round = 0;
    while (true) {
      round++;
      if (time_diff(start_time) > trial_time) {
        counts[i] = round;
        return;
      }
      if (i < p/2) {
        long sum = stm::atomic_read_only([&] {
                     long sum = 0;
                     for (int j=0; j < a.size(); j++)
                       sum += a[j].load();
                     return sum;});
        if (sum != expected) {
          std::cout << "bad sum, expected " << expected << " got " << sum << std::endl;
          abort();
      }
      } else {
        long l1 = parlay::hash<unsigned long>{}(i + p * (2*round)) % n;
        long l2 = parlay::hash<unsigned long>{}(i + p * (2*round+1)) % n;
        std::atomic([&] {
          long tmp = a[l1].load();
          a[l1].store(a[l2].load());
          a[l2].store(tmp);
          return;});
      }
    }}, 1);
  long total_scans = 0;
  long total_swaps = 0;
  for (int i = 0; i < p/2; i++) total_scans += counts[i];
  for (int i = p/2; i < p; i++) total_swaps += counts[i];
  
  std::cout << P.commandName() << " completed successfully: swaps = " << std::setprecision(3)
            << total_swaps/1e9 << "B, scans = " << total_scans/1e3 << "K" << std::endl;
}

    

