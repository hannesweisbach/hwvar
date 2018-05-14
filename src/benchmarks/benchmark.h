#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
  uint64_t size;
  double fill_factor;
  uint64_t line_size;
  int verbose;
} benchmark_config_t;

typedef struct {
  const char *const name;
  void (*init)(int argc, char *argv[], const benchmark_config_t *const config);
  void *(*init_arg)(void *);
  void (*reset_arg)(void *);
  void (*free_arg)(void *args);
  void *(*call)(void *arg);
  void *state;
} benchmark_t;

unsigned number_benchmarks(void);
void init_benchmarks(const int argc, char *argv[],
                     const benchmark_config_t *const config);
benchmark_t *get_benchmark_name(const char *const name);
benchmark_t *get_benchmark_idx(const unsigned idx);
void list_benchmarks(void);

unsigned tune_size(const char *const name,
                   const benchmark_config_t *const config,
                   const unsigned data_size, const uint16_t datasets,
                   const uint16_t power);

#ifdef __cplusplus
}
#endif
