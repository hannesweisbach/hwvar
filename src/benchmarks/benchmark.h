#pragma once

typedef struct {
  void *(*init_args)(int argc);
  void (*free_args)(void *args, int argc);
  void *(*get_arg)(void *args, int idx);
  void *(*call)(void *arg);
} benchmark_ops_t;
