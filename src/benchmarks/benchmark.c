#include <getopt.h>
#include <stdio.h>
#include <string.h>

#include <benchmark.h>

#include "dgemm.h"
#include "HACCmk.h"
#include "stream.h"
#include "sha256.h"

static benchmark_t *benchmarks[] = {&dgemm_ops,    &HACCmk_ops, &STREAM_Copy,
                                    &STREAM_Scale, &STREAM_Add, &STREAM_Triad,
                                    &STREAM,       &SHA256};

static unsigned number_benchmarks() {
  return sizeof(benchmarks) / sizeof(benchmark_t *);
}

void init_benchmarks(const int argc, char *argv[]) {
  unsigned num_benchmarks = number_benchmarks();

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];
    optind = 1;
    if (benchmark->init) {
      benchmark->init(argc, argv);
    }
  }
}

benchmark_t *get_benchmark(const char *const name) {
  unsigned num_benchmarks = number_benchmarks();

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];

    if (strcmp(name, benchmark->name) == 0) {
      return benchmark;
    }
  }
  return NULL;
}

void list_benchmarks() {
  unsigned num_benchmarks = number_benchmarks();

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    fprintf(stdout, "%s\n", benchmarks[i]->name);
  }
}
