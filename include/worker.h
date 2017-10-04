#pragma once
#include <pthread.h>
#include <stdint.h>

#include <hwloc.h>

#include <barrier.h>
#include <benchmark.h>

typedef struct work {
  pthread_barrier_t *barrier;
  benchmark_ops_t *ops;
  void *arg;
  uint64_t *result;
  unsigned reps;
  int padding__;
} work_t;

enum state { IDLE, QUEUED, WORKING, DONE };

struct arg {
  pthread_mutex_t lock;
  pthread_cond_t cv;
  work_t *work;
  unsigned thread;
  unsigned cpu;
  enum state s;
  hwloc_topology_t topology;
  hwloc_const_cpuset_t cpuset;
  char *cpuset_string;
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
  unsigned *logical_to_os;
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

void initialize_work(step_t *step, benchmark_ops_t *ops, void *arg,
                     hwloc_const_cpuset_t const cpuset, const int reps);
void run_work_rr(step_t *step, thread_data_t *threads,
                 hwloc_const_cpuset_t const cpuset);
void run_work_concurrent(step_t *step, thread_data_t *threads,
                         hwloc_const_cpuset_t const cpuset);

