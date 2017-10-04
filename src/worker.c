#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

#include <worker.h>
#include <arch.h>
#include <platform.h>

step_t *init_step(const int threads) {
  step_t *step = (step_t *)malloc(sizeof(step_t));
  if (step == NULL)
    return NULL;

  step->work = (work_t *)malloc(sizeof(work_t) * (unsigned)threads);
  if (step->work == NULL)
    return NULL;

  // TODO: error handling.
  pthread_barrier_init(&step->barrier, NULL, (unsigned)threads);
  pthread_mutex_init(&step->lock, NULL);
  pthread_cond_init(&step->cv, NULL);

  step->threads = threads;

  return step;
}

void queue_work(struct arg *arg, work_t *work) {
  pthread_mutex_lock(&arg->lock);
  assert(arg->work == NULL);
  assert(arg->s == IDLE);
  arg->work = work;
  arg->s = QUEUED;
  pthread_cond_signal(&arg->cv);
  pthread_mutex_unlock(&arg->lock);
}

work_t * wait_until_done(struct arg *arg) {
  pthread_mutex_lock(&arg->lock);
  while (arg->s != DONE) {
    pthread_cond_wait(&arg->cv, &arg->lock);
  }
  work_t *work = arg->work;
  arg->work = NULL;
  arg->s = IDLE;
  pthread_mutex_unlock(&arg->lock);

  return work;
}

static work_t *wait_for_work(struct arg *arg) {
  pthread_mutex_lock(&arg->lock);
  assert(arg->s != WORKING);
  while (arg->s != QUEUED) {
    pthread_cond_wait(&arg->cv, &arg->lock);
  }
  work_t *work = arg->work;
  arg->work = NULL;
  arg->s = WORKING;
  pthread_mutex_unlock(&arg->lock);

  return work;
}

static void return_finished_work(struct arg *arg, work_t *work) {
  pthread_mutex_lock(&arg->lock);
  assert(arg->work == NULL);
  assert(arg->s == WORKING);
  arg->work = work;
  arg->s = DONE;
  pthread_cond_signal(&arg->cv);
  pthread_mutex_unlock(&arg->lock);
}

static uint64_t get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

  return ts.tv_sec * 1000 * 1000 * 1000 + ts.tv_nsec;
}

void *worker(void *arg_) {
  // get & check cpu no
  // potentially set_affinity / report back cpu no?

  struct arg *arg = (struct arg *)arg_;
  arg->run = 1;

  pthread_mutex_lock(&arg->lock);
  int dirigent = arg->dirigent;
  fprintf(stderr, "Setting CPU for thread %s (%d)\n", arg->cpuset_string, arg->cpu);
  hwloc_set_cpubind(arg->topology, arg->cpuset, HWLOC_CPUBIND_THREAD);
  pthread_mutex_unlock(&arg->lock);

  while (arg->run) {
    work_t *work = wait_for_work(arg);

    void *benchmark_arg =
        (work->ops->init_arg) ? work->ops->init_arg(work->arg) : work->arg;

    const int err = pthread_barrier_wait(work->barrier);
    if (err && err != PTHREAD_BARRIER_SERIAL_THREAD) {
      perror("barrier");
    }

    for (unsigned rep = 0; rep < work->reps; ++rep) {
      // const uint64_t st = get_time();
      const uint64_t start = arch_timestamp_begin();
      work->ops->call(benchmark_arg);
      const uint64_t end = arch_timestamp_end();
      // const uint64_t et = get_time();
      if (work->result)
        work->result[rep] = end - start;
    }

    return_finished_work(arg, work);

    if (dirigent)
      break;
  }

  if (!dirigent)
    fprintf(stderr, "Thread %s stopped.\n", arg->cpuset_string);

  return NULL;
}

void initialize_work(step_t *step, benchmark_ops_t *ops, void *arg,
                     hwloc_const_cpuset_t const cpuset, const int reps) {
  unsigned int cpu;
  hwloc_bitmap_foreach_begin(cpu, cpuset) {
    work_t *work = &step->work[cpu];
    work->ops = ops;
    work->arg = arg;
    work->barrier = &step->barrier;
    work->reps = reps;
  }
  hwloc_bitmap_foreach_end();
}

