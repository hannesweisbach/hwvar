#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <config.h>

#ifdef HAVE_SCHED_H
#include <sched.h>
#endif

#include <hwloc.h>

#include <worker.h>
#include <arch.h>
#include <platform.h>
#include <mckernel.h>

step_t *init_step(const int threads) {
  step_t *step = (step_t *)malloc(sizeof(step_t));
  if (step == NULL)
    return NULL;

  step->work = (work_t *)malloc(sizeof(work_t) * (unsigned)threads);
  if (step->work == NULL)
    return NULL;

  // TODO: error handling.
  pthread_barrier_init(&step->barrier, NULL, (unsigned)threads);

  step->threads = threads;

  return step;
}

void free_step(step_t *step) {
  pthread_barrier_destroy(&step->barrier);
  free(step->work);
  free(step);
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

#ifndef __sparc
static uint64_t get_time() {
  struct timespec ts;
#ifdef HAVE_CLOCK_GETTIME
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
#endif

  return (uint64_t)ts.tv_sec * 1000 * 1000 * 1000 + (uint64_t)ts.tv_nsec;
}
#endif

static hwloc_cpuset_t current_cpuset_hwloc(hwloc_topology_t topology) {
  hwloc_cpuset_t ret = hwloc_bitmap_alloc();

  if (hwloc_get_cpubind(topology, ret, HWLOC_CPUBIND_THREAD)) {
    perror("hwloc_get_cpubind() failed");
  }

  return ret;
}

static unsigned current_cpu_hwloc(hwloc_topology_t topology) {
  hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
  if (hwloc_get_last_cpu_location(topology, cpuset, HWLOC_CPUBIND_THREAD)) {
    perror("hwloc_get_last_cpu_location() failed");
  }
  const int cpu = hwloc_bitmap_first(cpuset);
  if (cpu < 0) {
    perror("hwloc_bitmap_first() failed");
  }

  hwloc_bitmap_free(cpuset);

  return (unsigned)cpu;
}

#ifdef HAVE_SCHED_H
static hwloc_cpuset_t current_cpuset_getaffinity() {
  hwloc_cpuset_t ret = hwloc_bitmap_alloc();
  cpu_set_t cpuset;

  if (sched_getaffinity(0, sizeof(cpuset), &cpuset)) {
    perror("sched_getaffinity() failed");
  }

  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &cpuset)) {
      hwloc_bitmap_set(ret, (unsigned)cpu);
    }
  }

  return ret;
}

static unsigned current_cpu_getaffinity() {
  const int cpu = sched_getcpu();
  if (cpu < 0) {
    perror("sched_getcpu() failed");
  }

  return (unsigned)cpu;
}
#endif

static void bind_thread(struct arg *arg) {
  if (arg->do_binding) {
    if (hwloc_topology_is_thissystem(arg->topology)) {
      if (hwloc_set_cpubind(arg->topology, arg->cpuset, HWLOC_CPUBIND_THREAD)) {
        perror("hwloc_set_cpubind() failed");
      }
    } else {
#ifdef HAVE_SCHED_H
      cpu_set_t cpuset;
      CPU_ZERO(&cpuset);
      CPU_SET(arg->cpu, &cpuset);
      if (sched_setaffinity(0, sizeof(cpuset), &cpuset)) {
        perror("sched_setaffinity() failed");
      }
#endif
    }

    hwloc_cpuset_t hwloc_cpuset = current_cpuset_hwloc(arg->topology);
    const unsigned hwloc_cpu = current_cpu_hwloc(arg->topology);
    char hwloc_cpuset_str[16];
    hwloc_bitmap_snprintf(hwloc_cpuset_str, sizeof(hwloc_cpuset_str),
                          hwloc_cpuset);

#ifdef HAVE_SCHED_H
    hwloc_cpuset_t sched_cpuset = current_cpuset_getaffinity();
    const unsigned sched_cpu = current_cpu_getaffinity();
    char sched_cpuset_str[16];
    hwloc_bitmap_snprintf(sched_cpuset_str, sizeof(sched_cpuset_str),
                          sched_cpuset);
#endif

    const int equal =
        hwloc_bitmap_isequal(arg->cpuset, hwloc_cpuset)
        /* Unfortunately hwloc_get_last_cpu_location() does not (yet?) work
           under McKernel. */
        && (mck_is_mckernel() || (arg->cpu == hwloc_cpu))
#ifdef HAVE_SCHED_H
        && hwloc_bitmap_isequal(arg->cpuset, sched_cpuset) &&
        (arg->cpu == sched_cpu)
#endif
        ;

    if (!equal) {
      fprintf(stderr,
              "Target: %s/%02d, getaffinity/cpu(): %s/%02d, hwloc: %s/%02d\n",
              arg->cpuset_string, arg->cpu,
#ifdef HAVE_SCHED_H
              sched_cpuset_str, sched_cpu,
#else
              "N/A", -1,
#endif
              hwloc_cpuset_str, hwloc_cpu);
    } else {
      fprintf(stderr, "Thread bound to %s/%02d\n", arg->cpuset_string,
              arg->cpu);
    }
    hwloc_bitmap_free(hwloc_cpuset);
    hwloc_bitmap_free(sched_cpuset);
  } else {
    /* If there's no binding at least record the current CPU number */
#ifdef HAVE_SCHED_H
    arg->cpu = current_cpu_getaffinity();
#endif
  }
}

void *worker(void *arg_) {
  struct arg *arg = (struct arg *)arg_;
  arg->run = 1;

  pthread_mutex_lock(&arg->lock);
  int dirigent = arg->dirigent;

  if (dirigent && !arg->init) {
#if defined(PERF)
    const char *method = "perf";
#elif defined(JEVENTS)
    const char *method = "jevents";
#elif defined(RAWMSR)
    const char *method = "rawmsr";
#else
    const char *method = "none";
#endif
    fprintf(stderr, "PMU method: %s\n", method);
  }

  if (!arg->init) {
    bind_thread(arg);
    arg->init = 1;
  }

  pthread_mutex_unlock(&arg->lock);

  while (arg->run) {
    work_t *work = wait_for_work(arg);

    void *benchmark_arg =
        (work->ops->init_arg) ? work->ops->init_arg(work->arg) : work->arg;

    struct pmu *pmus = arch_pmu_init(work->pmcs, work->num_pmcs, arg->cpu);

    {
      const int err = pthread_barrier_wait(work->barrier);
      if (err && err != PTHREAD_BARRIER_SERIAL_THREAD) {
        perror("pthread_barrier_wait() failed");
      }
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

