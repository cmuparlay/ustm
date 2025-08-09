#include <chrono>

#include "parse_command_line.h"

#define MAP_TYPE RMap

#include "tpcc/TPCC.hpp"
#include "tpcc/TestConfig.hpp"
#include "transactions.h"
#include "parlay/parallel.h"
#include "parlay/primitives.h"

int main(int argc, char* argv[]) {
  commandLine P(argc,argv,"");
  double time = P.getOptionDoubleValue("-t", 1.0);
  int threads = P.getOptionIntValue("-p", parlay::num_workers());
  int rounds = P.getOptionIntValue("-r", 1);
  int warmup = !P.getOption("-nowarmup");
  bool verbose = P.getOption("-v");
  int num_warehouses = P.getOptionIntValue("-num_warehouses", threads);
  int tmp = P.getOptionIntValue("-trans", 0);
  if (tmp != 0) num_warehouses = tmp;
  int new_order = P.getOptionIntValue("-new_order", 50);
  int payment = 100 - new_order;

  for (int i=0; i < rounds + warmup; i++) {
    tpcc::TPCC tpcc_struct(new_order, payment, 0, 0, 0);
    auto gtc = new GlobalTestConfig();
    gtc->task_num = threads;
    gtc->interval = time;
    gtc->num_warehouses = num_warehouses;
    gtc->verbose = verbose;
    tpcc_struct.init(gtc);

    if (verbose) tpcc_struct.table_sizes();

    std::cout << "here1" << std::endl;
#ifdef UEPOCH_H_
    long cnt = uepoch::clear();
#endif
    std::cout << "here2" << std::endl;

    auto startTime = std::chrono::high_resolution_clock::now();
    gtc->finish = startTime + std::chrono::seconds{(uint64_t)gtc->interval};
    stm::statistics();
    parlay::sequence<long> ops(threads);
    
    parlay::parallel_for(0, threads, [&] (long i) {
      auto ltc = new LocalTestConfig();
      ltc->tid=i;
      ltc->seed=2*i;
      std::cout << "here3" << std::endl;
      ops[i] = tpcc_struct.execute(gtc,ltc);
    }, 1, true);

    auto [total_aborts, total_ops_] = stm::statistics();
    long total_ops = parlay::reduce(ops);
    if (total_ops != total_ops_)
      std::cout << "inconsistent total ops: " << total_ops_ << ", " << total_ops << std::endl;
  
    if (verbose) tpcc_struct.table_sizes();
    if (i >= warmup)
      std::cout << P.commandName()
		<< std::setprecision(4)
		<< ": mops/sec = " << total_ops/( time * 1e6)
                << ", num_warehouses = " << num_warehouses
		<< ", %aborts = " << total_aborts * 100.0 / total_ops
		<< std::endl;
    tpcc_struct.cleanup(gtc);
#ifdef UEPOCH_H_
    cnt = uepoch::clear();
    std::cout << "after run: " << cnt << std::endl;
#endif
  }
}
