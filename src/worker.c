#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
#ifdef HAVE_CLOCK_GETTIME
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#endif

  return (uint64_t)ts.tv_sec * 1000 * 1000 * 1000 + (uint64_t)ts.tv_nsec;
}

void *worker(void *arg_) {
  // get & check cpu no
  // potentially set_affinity / report back cpu no?

  struct arg *arg = (struct arg *)arg_;
  arg->run = 1;

  pthread_mutex_lock(&arg->lock);
  int dirigent = arg->dirigent;
  fprintf(stderr, "Binding thread %d to CPU %s\n", arg->thread, arg->cpuset_string);
  /*
   * hwloc_cpuset_t current = hwloc_cpuset_alloc();
   * int hwloc_get_last_cpu_location(arg->topology,current,
   *                                 HWLOC_CPUBIND_THREAD);
   */
  int err = hwloc_set_cpubind(arg->topology, arg->cpuset, HWLOC_CPUBIND_THREAD);
  if (err) {
    fprintf(stderr, "Error binding thread %d to CPU %d: %d\n", arg->thread,
            arg->cpu, err);
  }

  pthread_mutex_unlock(&arg->lock);

  while (arg->run) {
    work_t *work = wait_for_work(arg);

    void *benchmark_arg =
        (work->ops->init_arg) ? work->ops->init_arg(work->arg) : work->arg;

    void *pmus = arch_pmu_init(work->pmcs, work->num_pmcs);

    err = pthread_barrier_wait(work->barrier);
    if (err && err != PTHREAD_BARRIER_SERIAL_THREAD) {
      perror("barrier");
    }

    // reps = warm-up + benchmark runs.
    for (unsigned rep = 0; rep < work->reps; ++rep) {
      if (work->ops->reset_arg) {
        work->ops->reset_arg(benchmark_arg);
      }

      const uint64_t offset = (work->num_pmcs + 1) * rep;
      if (work->result) {
        arch_pmu_begin(pmus, &work->result[offset + 1]);
      }
      const uint64_t start = arch_timestamp_begin();
      work->ops->call(benchmark_arg);
      const uint64_t end = arch_timestamp_end();
      if (work->result) {
        arch_pmu_end(pmus, &work->result[offset + 1]);
        work->result[offset] = end - start;
      }
    }

    arch_pmu_free(pmus);
    return_finished_work(arg, work);

    if (dirigent)
      break;
  }

  if (!dirigent)
    fprintf(stderr, "Thread %s stopped.\n", arg->cpuset_string);

  return NULL;
}

