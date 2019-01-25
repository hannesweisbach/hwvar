#include <assert.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include <benchmark.h>

#include "HACCmk.h"
#include "dgemm.h"
#include "fwq.h"
#include "hpccg.h"
#include "minife.h"
#include "sha256.h"
#include "stream.h"
#include "capacity.h"
#include "syscall.h"

static benchmark_t *benchmarks[] = {
    &dgemm_ops,  &HACCmk_ops,   &SHA256,      &fwq_ops,      &hpccg_ops,
    &minife_ops, &capacity_ops, &syscall_ops,
    &STREAM,     &STREAM_Scale, &STREAM_Add,  &STREAM_Triad, &STREAM_Copy};

unsigned number_benchmarks() {
  return sizeof(benchmarks) / sizeof(benchmark_t *);
}

void init_benchmarks(const int argc, char *argv[],
                     const benchmark_config_t *const config) {
  /* 5 stream benchmarks share the same init code */
  unsigned num_benchmarks = number_benchmarks() - 4;

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];
    optind = 1;
    if (benchmark->init) {
      benchmark->init(argc, argv, config);
    }
  }
}

benchmark_t *get_benchmark_name(const char *const name) {
  unsigned num_benchmarks = number_benchmarks();

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];

    if (strcasecmp(name, benchmark->name) == 0) {
      return benchmark;
    }
  }
  return NULL;
}

benchmark_t *get_benchmark_idx(const unsigned idx) {
  assert(idx < number_benchmarks());
  return benchmarks[idx];
}

void list_benchmarks() {
  unsigned num_benchmarks = number_benchmarks();

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    fprintf(stdout, "%s\n", benchmarks[i]->name);
  }
}

/**
 * Calculate input size of a benchmark.
 *
 * Calculates the input size of a benchmark, such that the cache is 90% full,
 * with the intention of leaving some space for stack, etc … in the cache. At
 * the same time, the routine calculates the benchmark size such that an integer
 * number of cache lines are used.
 * As a diagnostic the calculated number of bytes and the fill level of the
 * cache are printed.
 *
 * @param name name of the benchmark
 * @param cache_size  total cache size
 * @param cache_line_size size of a cache line
 * @param power power with which the size argument goes into the memory
 *              requirement of the benchmark, i.e. 1 for vectors, 2 for square
 *              matrices.
 * @param data_size sizeof() of the data type used by the benchmark.
 * @param datasets number of datasets used by the benchmark, i.e. number of
 *                  vectors, matrices, …
 **/
unsigned tune_size(const char *const name,
                   const benchmark_config_t *const config,
                   const unsigned data_size, const uint16_t datasets,
                   const uint16_t power) {
  const double cache_size_ = config->size * config->fill_factor;
  const double elems_per_set = cache_size_ / datasets / data_size;
  const double elems_per_dim = pow(elems_per_set, 1.0 / power);
  const unsigned num_lines =
      ((unsigned)elems_per_dim / (config->line_size / data_size));
  const unsigned bytes_per_dim_aligned = num_lines * config->line_size;
  const unsigned n = bytes_per_dim_aligned / data_size;
  const double bytes = pow(n, power) * data_size * datasets;
  const double p = bytes * 100.0 / config->size;

  if (config->verbose) {
    fprintf(stderr, "[Size] %s requires %u bytes of %" PRIu64 "k (%5.1f%%)\n",
            name, (unsigned)bytes, config->size / 1024, p);
  }

  return n;
}

