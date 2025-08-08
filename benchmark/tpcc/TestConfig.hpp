#ifndef GLOBALTESTCONFIG_HPP
#define GLOBALTESTCONFIG_HPP

#include <chrono>

struct GlobalTestConfig {
public:
  int task_num = 4;  // number of threads
  int num_warehouses = 1;  // number of threads
  std::chrono::time_point<std::chrono::high_resolution_clock> start;
  //std::atomic<std::chrono::time_point<std::chrono::high_resolution_clock>> finish;  // timing structures
  std::chrono::time_point<std::chrono::high_resolution_clock> finish;  // timing structures
  double interval = 2.0;  // number of seconds to run test
  int verbose=0;
  GlobalTestConfig() {};
  ~GlobalTestConfig() {};
};

// local test configuration, one per thread
struct LocalTestConfig {
public:
  int tid;
  unsigned int seed;
};

#endif
