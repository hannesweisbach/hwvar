#pragma once

typedef struct {
  void *(*init_arg)(void *);
  void (*free_arg)(void *args);
  void *(*get_arg)(void *args, int idx);
  void *(*call)(void *arg);
} benchmark_ops_t;
