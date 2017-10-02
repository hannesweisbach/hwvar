#include <stdio.h>
#include <stdlib.h>

#include <platform.h>
#include <worker.h>

#include "dgemm.h"

static thread_info_t *spawn_workers(const int cpus) {
  thread_info_t *threads =
      (thread_info_t *)malloc(sizeof(thread_info_t) * (const unsigned)cpus);
  if (threads == NULL)
    return NULL;

  int cpu = 0;
  for (; cpu < cpus; ++cpu) {
    thread_info_t *thread = &threads[cpu];
    // TODO handle errors.
    pthread_mutex_init(&thread->thread_arg.lock, NULL);
    pthread_cond_init(&thread->thread_arg.cv, NULL);

    pthread_mutex_lock(&thread->thread_arg.lock);
    thread->thread_arg.work = NULL;
    thread->thread_arg.s = IDLE;
    thread->thread_arg.run = 1;
    thread->thread_arg.dirigent = cpu == 0;
    thread->thread_arg.thread = cpu;
    thread->thread_arg.cpu = cpu;
    pthread_mutex_unlock(&thread->thread_arg.lock);

    if (cpu > 0) {
      int err =
          pthread_create(&thread->thread, NULL, worker, &thread->thread_arg);
      if (err) {
        fprintf(stderr, "Error creating thread.\n");
        // can't cancel threads; just quit.
        return NULL;
      }
    }
  }

  return threads;
}

static void *stop_thread(void *arg_) {
  struct arg *arg = (struct arg *)arg_;
  arg->run = 0;
  fprintf(stdout, "Thread %d ist stopping.\n", arg->cpu);
  return NULL;
}

static int stop_single_worker(thread_info_t *thread, step_t *step) {
  step->work->func = stop_thread;
  step->work->arg = &thread->thread_arg;
  step->work->barrier = &step->barrier;
  step->work->reps = 1;

  queue_work(&thread->thread_arg, step->work);
  wait_until_done(&thread->thread_arg);

  int err = pthread_join(thread->thread, NULL);
  if (err) {
    fprintf(stderr, "Error joining with thread on CPU %d\n",
            thread->thread_arg.cpu);
    return err;
  }

  pthread_mutex_destroy(&thread->thread_arg.lock);
  pthread_cond_destroy(&thread->thread_arg.cv);
  if (err) {
    fprintf(stderr, "Error destroying queue for thread on CPU %d\n",
            thread->thread_arg.cpu);
  }

  return err;
}

static void stop_workers(thread_info_t *threads, int cpus) {
  step_t *step = init_step(1);

  int err = 0;
  for (int cpu = 1; cpu < cpus; ++cpu) {
    err |= stop_single_worker(&threads[cpu], step);
    printf("CPU %d stopped\n", cpu);
  }
  if (!err) {
    free(threads);
  }
}

static void run_in_parallel(thread_info_t *workers, benchmark_ops_t *ops,
                            int cpus) {
  void *arg_vector = ops->init_args(cpus);
  step_t *step = init_step(cpus);

  for (int i = 0; i < cpus; ++i) {
    work_t *work = &step->work[i];
    work->func = ops->call;
    work->arg = ops->get_arg(arg_vector, i);
    work->barrier = &step->barrier;
    work->reps = 1;
  }

  run_work_concurrent(step, workers, 0x3);
}

int main() {
  printf("%ld\n", sizeof(pthread_mutex_t));
  printf("%ld\n", sizeof(pthread_cond_t));

  printf("CPU %d\n", platform_get_number_cpus());
  printf("CPU %d\n", platform_get_current_cpu());

  thread_info_t *workers = spawn_workers(2);

  run_in_parallel(workers, &dgemm_ops, 2);

  stop_workers(workers, 2);
}
