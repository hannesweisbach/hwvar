#pragma once

typedef struct {
  const char *const name;
  void (*init)(int argc, char *argv[]);
  void *(*init_arg)(void *);
  void (*reset_arg)(void *);
  void (*free_arg)(void *args);
  void *(*call)(void *arg);
  void *state;
} benchmark_t;

void init_benchmarks(const int argc, char *argv[]);
benchmark_t *get_benchmark(const char *const name);
void list_benchmarks(void);
