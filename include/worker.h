#pragma once
#include <pthread.h>
#include <stdint.h>

#include <hwloc.h>

#include <barrier.h>
#include <benchmark.h>

typedef struct work {
  pthread_barrier_t *barrier;
  benchmark_t *ops;
  void *arg;
  uint64_t *result;
  unsigned reps;
  unsigned counters;
} work_t;

enum state { IDLE, QUEUED, WORKING, DONE };

struct arg {
  hwloc_topology_t topology;
  hwloc_const_cpuset_t cpuset;
  char *cpuset_string;
  pthread_mutex_t lock;
  pthread_cond_t cv;
  work_t *work;
  unsigned thread;
  unsigned cpu;
  enum state s;
  short run;
  short dirigent;
};

typedef struct thread_data {
  pthread_t thread;
  struct arg thread_arg;
} thread_data_t;

typedef struct threads {
  thread_data_t *threads;
  hwloc_const_cpuset_t cpuset;
} threads_t;

typedef struct step {
  pthread_barrier_t barrier;
  pthread_mutex_t lock;
  pthread_cond_t cv;
  struct work *work;
  int threads;
  int padding__;
} step_t;

void *worker(void *arg_);

step_t *init_step(const int threads);
void queue_work(struct arg *arg, work_t *work);
work_t * wait_until_done(struct arg *arg);

