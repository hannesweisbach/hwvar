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

void *worker(void *arg_) {
  // get & check cpu no
  // potentially set_affinity / report back cpu no?

  struct arg *arg = (struct arg *)arg_;
  arg->run = 1;

  int cpu = platform_get_current_cpu();
  pthread_mutex_lock(&arg->lock);
  int target_cpu = arg->cpu;
  int dirigent = arg->dirigent;
  pthread_mutex_unlock(&arg->lock);

  if (target_cpu != cpu) {
#ifdef HAVE_SCHED_SETAFFINITY
    // do it always?
    // report back the cpu number instead?
    cpu_set_t set;
    CPU_ZERO(&set);
    CPU_SET(target_cpu, &set);
    int err = sched_setaffinity(0, sizeof(set), &set);
#else
    fprintf(stderr, "Expected CPU %d but running on CPU %d\n", arg->cpu, cpu);
#endif
  }

  while (arg->run) {
    work_t *work = wait_for_work(arg);
    assert(pthread_barrier_wait(work->barrier) >= 0);

    for (int rep = 0; rep < work->reps; ++rep) {
      const uint64_t start = arch_timestamp_begin();
      work->func(work->arg);
      const uint64_t end = arch_timestamp_end();
      printf("%d %lld\n", cpu, end - start);
    }
    // report results

    return_finished_work(arg, work);

    if (dirigent)
      break;
  }

  if (!dirigent)
    fprintf(stdout, "Thread %d stopped.\n", arg->cpu);

  return NULL;
}

void initialize_work(step_t *step, work_func_t func, void *arg,
                     const uint64_t cpuset, const int reps) {
  for (int cpu = 0; cpu < 64; ++cpu) {
    if (!(cpuset & (1ULL << cpu)))
      continue;
    work_t *work = &step->work[cpu];
    work->func = func;
    work->arg = arg;
    work->barrier = &step->barrier;
    work->reps = reps;
  }
}

void run_work_rr(step_t *step, thread_info_t *threads, uint64_t cpuset) {
  for (int cpu = 0; cpu < 64; ++cpu) {
    if (!(cpuset & (1ULL << cpu)))
      continue;
    struct arg *arg = &threads[cpu].thread_arg;
    queue_work(arg, &step->work[cpu]);
    if (cpu == 0) { // run dirigent
      worker(arg);
    }
    wait_until_done(arg);
  }
}

void run_work_concurrent(step_t *step, thread_info_t *threads,
                         uint64_t cpuset) {
  for (int cpu = 0; cpu < 64; ++cpu) {
    if (!(cpuset & (1ULL << cpu)))
      continue;
    queue_work(&threads[cpu].thread_arg, &step->work[cpu]);
  }

  // run dirigent
  worker(&threads[0].thread_arg);

  for (int cpu = 0; cpu < 64; ++cpu) {
    if (!(cpuset & (1ULL << cpu)))
      continue;
    wait_until_done(&threads[cpu].thread_arg);
  }
}
