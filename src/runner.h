#pragma once

#include <vector>
#include <thread>
#include <memory>

#include <gsl/gsl>

#include "hwloc"
#include "pmcs.h"
#include <benchmark.h>

class benchmark_result {
  std::unique_ptr<hwloc::cpuset> cpus_;
  const pmc *pmcs_;
  unsigned repetitions_;
  std::vector<uint64_t> data_;

public:
  benchmark_result(const hwloc::cpuset &cpus, const pmc &pmcs,
                   const unsigned repetitions);
  gsl::span<uint64_t> buffer_for_thread(const int i);

  friend std::ostream &operator<<(std::ostream &os,
                                  const benchmark_result &result);
};

class runner {
  class executor;
  std::vector<std::unique_ptr<executor>> threads_;
  hwloc::cpuset cpuset_;

public:
  runner(hwloc::topology *const toplogy, const hwloc::cpuset &cpuset,
         bool include_hyperthreads = true, bool do_binding = true);
  ~runner();

  std::unique_ptr<benchmark_result>
  serial(benchmark_t *ops, const unsigned reps, const pmc &pmcs);
  std::unique_ptr<benchmark_result> parallel(benchmark_t *ops,
                                             const unsigned reps,
                                             const pmc &pmcs);
  std::unique_ptr<benchmark_result>
  parallel(benchmark_t *ops1, benchmark_t *ops2, const hwloc::bitmap &cpuset1,
           const hwloc::cpuset &cpuset2, const unsigned reps, const pmc &pmcs);
};
