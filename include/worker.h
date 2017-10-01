#pragma once
#include <pthread.h>
#include <stdint.h>

#include <barrier.h>

typedef void *(*work_func_t)(void *);

typedef struct work {
  work_func_t func;
  void *arg;
  pthread_barrier_t *barrier;
  int reps;
  int padding__;
} work_t;

enum state { IDLE, WORKING, DONE };

struct arg {
  pthread_mutex_t lock;
  pthread_cond_t cv;
  work_t *work;
  int thread;
  enum state s;
  int cpu;
  short run;
  short dirigent;
};

typedef struct thread_info {
  pthread_t thread;
  struct arg thread_arg;
} thread_info_t;

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

void initialize_work(step_t *step, work_func_t func, void *arg,
                     const uint64_t cpuset, const int reps);
void run_work_rr(step_t *step, thread_info_t *threads, uint64_t cpuset);
void run_work_concurrent(step_t *step, thread_info_t *threads, uint64_t cpuset);

