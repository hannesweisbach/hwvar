#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include <benchmark.h>

#include "dgemm.h"
#include "HACCmk.h"

static benchmark_t *benchmarks[] = {&dgemm_ops, &HACCmk_ops};

void init_benchmarks(const int argc, char *argv[]) {
  unsigned num_benchmarks = sizeof(benchmarks) / sizeof(benchmark_t *);

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];
    optind = 1;
    if (benchmark->init) {
      benchmark->init(argc, argv);
    }
  }
}

benchmark_t *get_benchmark(const char *const name) {
  unsigned num_benchmarks = sizeof(benchmarks) / sizeof(benchmark_t *);

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];

    if (strcmp(name, benchmark->name) == 0) {
      return benchmark;
    }
  }
  return NULL;
}

void list_benchmarks() {
  unsigned num_benchmarks = sizeof(benchmarks) / sizeof(benchmark_t *);

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    fprintf(stdout, "%s\n", benchmarks[i]->name);
  }
}
