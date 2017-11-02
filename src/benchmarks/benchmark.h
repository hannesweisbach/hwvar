#pragma once

#include <stdint.h>

typedef struct {
  unsigned data_size;
  uint16_t datasets;
  uint16_t power;
} tuning_param_t;

typedef struct {
  const char *const name;
  void (*init)(int argc, char *argv[]);
  void *(*init_arg)(void *);
  void (*reset_arg)(void *);
  void (*free_arg)(void *args);
  void *(*call)(void *arg);
  void *state;
  tuning_param_t params;
} benchmark_t;

unsigned number_benchmarks(void);
void init_benchmarks(const int argc, char *argv[]);
benchmark_t *get_benchmark_name(const char *const name);
benchmark_t *get_benchmark_idx(const unsigned idx);
void list_benchmarks(void);
