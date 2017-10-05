#pragma once

typedef struct {
  const char *const name;
  void (*init)(int argc, char *argv[]);
  void *(*init_arg)(void *);
  void (*free_arg)(void *args);
  void *(*get_arg)(void *args, int idx);
  void *(*call)(void *arg);
  void *state;
} benchmark_t;

