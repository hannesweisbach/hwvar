#include <getopt.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <hwloc.h>

#include <platform.h>
#include <worker.h>
#include "benchmark.h"

#include "dgemm.h"

static threads_t *spawn_workers(hwloc_topology_t topology) {
  const int depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_PU);
  const unsigned num_threads =
      hwloc_get_nbobjs_by_depth(topology, (unsigned)depth);

  threads_t *workers = (threads_t *)malloc(sizeof(threads_t));
  if (workers == NULL) {
    exit(EXIT_FAILURE);
  }

  fprintf(stderr, "Spawning %d workers\n", num_threads);

  workers->threads =
      (thread_data_t *)malloc(sizeof(thread_data_t) * num_threads);
  if (workers->threads == NULL) {
    exit(EXIT_FAILURE);
  }

  workers->logical_to_os = (unsigned *)malloc(sizeof(unsigned) * num_threads);
  if (workers->logical_to_os == NULL) {
    exit(EXIT_FAILURE);
  }

  hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
  for (unsigned i = 0; i < num_threads; ++i) {
    /* TODO Awesome:
     * If no object for that type exists, NULL is returned. If there
     * are several levels with objects of that type, NULL is returned and ther
     * caller may fallback to hwloc_get_obj_by_depth(). */
    hwloc_obj_t obj = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, i);
    if (obj == NULL) {
      printf("Error getting obj. Implement fallback to "
             "hwloc_get_obj_by_depth()?\n");
      exit(EXIT_FAILURE);
    }
    thread_data_t *thread = &workers->threads[i];
    // TODO handle errors.
    pthread_mutex_init(&thread->thread_arg.lock, NULL);
    pthread_cond_init(&thread->thread_arg.cv, NULL);

    pthread_mutex_lock(&thread->thread_arg.lock);
    thread->thread_arg.work = NULL;
    thread->thread_arg.s = IDLE;
    thread->thread_arg.run = 1;
    thread->thread_arg.dirigent = i == 0;
    thread->thread_arg.thread = i;
    thread->thread_arg.cpu = obj->os_index;
    thread->thread_arg.topology = topology;
    {
      hwloc_cpuset_t tmp = hwloc_bitmap_dup(obj->cpuset);
      // TODO might this get the same mask for two sets?!
      hwloc_bitmap_singlify(tmp);
      thread->thread_arg.cpuset = tmp;
    }
    hwloc_bitmap_asprintf(&thread->thread_arg.cpuset_string, cpuset);
    fprintf(stderr, "Found L:%u P:%u %s\n", obj->logical_index, obj->os_index,
            thread->thread_arg.cpuset_string);

    hwloc_bitmap_set(cpuset, i);
    pthread_mutex_unlock(&thread->thread_arg.lock);

    workers->logical_to_os[obj->logical_index] = obj->os_index;

    if (i > 0) {
      int err =
          pthread_create(&thread->thread, NULL, worker, &thread->thread_arg);
      if (err) {
        fprintf(stderr, "Error creating thread.\n");
        // can't cancel threads; just quit.
        return NULL;
      }
    }
  }

  workers->cpuset = cpuset;

  return workers;
}

#include <benchmark.h>

static void *stop_thread(void *arg_) {
  struct arg *arg = (struct arg *)arg_;
  arg->run = 0;
  return NULL;
}

static int stop_single_worker(thread_data_t *thread, step_t *step) {
  benchmark_t stop_ops = {"stop", NULL, NULL, NULL, NULL, stop_thread, NULL};

  step->work->ops = &stop_ops;
  step->work->arg = &thread->thread_arg;
  step->work->barrier = &step->barrier;
  step->work->reps = 1;
  step->work->result = NULL;

  queue_work(&thread->thread_arg, step->work);
  wait_until_done(&thread->thread_arg);

  int err = pthread_join(thread->thread, NULL);
  if (err) {
    fprintf(stderr, "Error joining with thread %d\n",
            thread->thread_arg.thread);
    return err;
  }

  pthread_mutex_destroy(&thread->thread_arg.lock);
  pthread_cond_destroy(&thread->thread_arg.cv);
  if (err) {
    fprintf(stderr, "Error destroying queue for thread on CPU %d\n",
            thread->thread_arg.thread);
  }

  return err;
}

static void stop_workers(threads_t *workers) {
  step_t *step = init_step(1);

  unsigned int i;
  int err = 0;
  hwloc_bitmap_foreach_begin(i, workers->cpuset) {
    if (workers->threads[i].thread_arg.dirigent) { // skip dirigent
      continue;
    }
    err |= stop_single_worker(&workers->threads[i], step);
  }
  hwloc_bitmap_foreach_end();

  if (!err) {
    // TODO cleanup.
  }
}

typedef struct {
  uint64_t *data;
  unsigned threads;
  unsigned repetitions;
} benchmark_result_t;

static benchmark_result_t result_alloc(unsigned threads,
                                       const unsigned repetitions) {
  benchmark_result_t result = {
      .data = NULL, .threads = threads, .repetitions = repetitions};

  result.data = (uint64_t *)malloc(sizeof(uint64_t) * threads * repetitions);

  return result;
}

static void result_print(FILE *file, threads_t *threads,
                         benchmark_result_t result,
                         hwloc_const_cpuset_t cpuset) {
  int cpu = -1;
  for (unsigned thread = 0; thread < result.threads; ++thread) {
    cpu = hwloc_bitmap_next(cpuset, cpu);
    fprintf(file, "%2d ", threads->logical_to_os[cpu]);
    for (unsigned rep = 0; rep < result.repetitions; ++rep) {
      fprintf(file, "%10" PRIu64 " ", result.data[thread * result.repetitions + rep]);
    }
    fprintf(file, "\n");
  }
}

static benchmark_result_t run_in_parallel(threads_t *workers, benchmark_t *ops,
                                          const unsigned repetitions) {
  int cpus = hwloc_bitmap_weight(workers->cpuset);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(cpus);

  unsigned int cpu;
  unsigned i = 0;
  hwloc_bitmap_foreach_begin(cpu, workers->cpuset) {
    work_t *work = &step->work[i];
    work->ops = ops;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[i * repetitions];
    work->reps = repetitions;

    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
    i = i + 1;
  }
  hwloc_bitmap_foreach_end();

  // run dirigent
  hwloc_bitmap_foreach_begin(i, workers->cpuset) {
    if (workers->threads[i].thread_arg.dirigent/* &&
        hwloc_bitmap_isset(cpuset, i)*/) {
      worker(&workers->threads[i].thread_arg);
      break;
    }
  }
  hwloc_bitmap_foreach_end();

  hwloc_bitmap_foreach_begin(i, workers->cpuset) {
    wait_until_done(&workers->threads[i].thread_arg);
  }
  hwloc_bitmap_foreach_end();

  return result;
}

static benchmark_result_t run_one_by_one(threads_t *workers, benchmark_t *ops,
                                         unsigned repetitions) {
  int cpus = hwloc_bitmap_weight(workers->cpuset);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(1);

  unsigned int i = 0;
  unsigned cpu;
  hwloc_bitmap_foreach_begin(cpu, workers->cpuset) {
    work_t *work = &step->work[0];
    work->ops = ops;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[i * repetitions];
    work->reps = repetitions;

    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
    if (arg->dirigent) { // run dirigent
      worker(arg);
    }
    wait_until_done(arg);
    i = i + 1;
  }
  hwloc_bitmap_foreach_end();

  return result;
}

static benchmark_result_t
run_two_benchmarks(threads_t *workers, benchmark_t *ops1, benchmark_t *ops2,
                   hwloc_const_cpuset_t set1, hwloc_const_cpuset_t set2,
                   const unsigned repetitions) {
  assert(!hwloc_bitmap_intersects(set1, set2));
  hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
  hwloc_bitmap_or(cpuset, set1, set2);
  hwloc_bitmap_and(cpuset, cpuset, workers->cpuset);
  assert(hwloc_bitmap_isincluded(cpuset, workers->cpuset));
  int cpus = hwloc_bitmap_weight(cpuset);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(cpus);

  unsigned int cpu;
  unsigned i = 0;
  hwloc_bitmap_foreach_begin(cpu, cpuset) {
    work_t *work = &step->work[i];
    work->ops = hwloc_bitmap_isset(set1, cpu) ? ops1 : ops2;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[i * repetitions];
    work->reps = repetitions;

    struct arg *arg = &workers->threads[cpu].thread_arg;
    queue_work(arg, work);
    i = i + 1;
  }
  hwloc_bitmap_foreach_end();

  // run dirigent
  hwloc_bitmap_foreach_begin(i, workers->cpuset) {
    if (workers->threads[i].thread_arg.dirigent &&
        hwloc_bitmap_isset(cpuset, i)) {
      worker(&workers->threads[i].thread_arg);
      break;
    }
  }
  hwloc_bitmap_foreach_end();

  hwloc_bitmap_foreach_begin(i, cpuset) {
    wait_until_done(&workers->threads[i].thread_arg);
  }
  hwloc_bitmap_foreach_end();

  hwloc_bitmap_free(cpuset);

  return result;
}

static int file_exists(const char *restrict name) {
  struct stat tmp;
  int err = stat(name, &tmp);
  return (err == 0) || (errno != ENOENT);
}

static unsigned count_chars(const char *str, char c) {
  if (str == NULL)
    return 0;
  unsigned int chars = 0;
  for (int i = 0; str[i] != '\0'; ++i) {
    if (str[i] == c) {
      ++chars;
    }
  }
  return chars;
}

int main(int argc, char *argv[]) {
  hwloc_topology_t topology;
  if (hwloc_topology_init(&topology)) {
    printf("hwloc_topology_init failed\n");
    exit(EXIT_FAILURE);
  }
  if (hwloc_topology_load(topology)) {
    printf("hwloc_topology_load() failed\n");
    exit(EXIT_FAILURE);
  }

  hwloc_const_cpuset_t orig = hwloc_topology_get_complete_cpuset(topology);
  if (hwloc_bitmap_weight(orig) == 48) {
    hwloc_cpuset_t restricted = hwloc_bitmap_dup(orig);
    hwloc_bitmap_clr(restricted, 0);
    hwloc_topology_restrict(topology, restricted, 0);
  }

  enum policy { PARALLEL, ONE_BY_ONE, PAIR, NR_POLICIES };

  enum policy policy = PARALLEL;
  benchmark_t **benchmarks = NULL;
  unsigned num_benchmarks = 0;
  unsigned iterations = 10;
  FILE *output = stdout;
  hwloc_cpuset_t cpuset1 = hwloc_bitmap_alloc();
  hwloc_cpuset_t cpuset2 = hwloc_bitmap_alloc();
  hwloc_cpuset_t runset = hwloc_bitmap_alloc();

  static struct option options[] = {
      {"policy", required_argument, NULL, 'p'},
      {"benchmarks", required_argument, NULL, 'b'},
      {"iterations", required_argument, NULL, 'i'},
      {"list-benchmarks", no_argument, NULL, 'l'},
      {"output", required_argument, NULL, 'o'},
      {"cpuset-1", required_argument, NULL, 1},
      {"cpuset-2", required_argument, NULL, 2},
      {NULL, 0, NULL, 0}};

  opterr = 0;

  while (1) {
    int c = getopt_long(argc, argv, "p:b:i:lo:", options, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 1:
      if (hwloc_bitmap_list_sscanf(cpuset1, optarg) < 0) {
        fprintf(stderr, "Error parsing cpuset %s\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 2:
      if (hwloc_bitmap_list_sscanf(cpuset2, optarg) < 0) {
        fprintf(stderr, "Error parsing cpuset %s\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'p':
      if (strcmp(optarg, "parallel") == 0) {
        policy = PARALLEL;
      } else if (strcmp(optarg, "onebyone") == 0) {
        policy = ONE_BY_ONE;
      } else if (strcmp(optarg, "pair") == 0) {
        policy = PAIR;
      } else {
        fprintf(stderr, "Unkown policy: %s\n", optarg);
        exit(EXIT_FAILURE);
      }
      break;
    case 'b':
      num_benchmarks = count_chars(optarg, ',') + 1;
      benchmarks =
          (benchmark_t **)malloc(sizeof(benchmark_t *) * num_benchmarks);
      if (benchmarks == NULL) {
        fprintf(stderr, "Error allocating memory");
        exit(EXIT_FAILURE);
      }
      char *arg = strtok(optarg, ",");
      unsigned i = 0;
      for (i = 0; arg != NULL && i < num_benchmarks; ++i) {
        benchmarks[i] = get_benchmark(arg);
        if (benchmarks[i] == NULL) {
          fprintf(stderr, "Benchmark %s unknown. Skipping.\n", arg);
          --i;
        }
        arg = strtok(NULL, ",");
      }
      num_benchmarks = i;
      break;
    case 'i':
      errno = 0;
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > UINT_MAX) {
        fprintf(stderr, "Could not parse --iterations argument '%s': %s\n",
                optarg, strerror(errno));
      }
      iterations = (unsigned)tmp;
      break;
    case 'l':
      list_benchmarks();
      exit(EXIT_SUCCESS);
    case 'o':
      if (strcmp(optarg, "-") == 0) {
        break;
      } else if (file_exists(optarg)) {
        fprintf(stderr, "File %s already exists.\n", optarg);
        exit(EXIT_FAILURE);
      } else {
        output = fopen(optarg, "w");
      }
    case ':':
      break;
    default:
      break;
    }
  }

  if (benchmarks == NULL || num_benchmarks == 0) {
    fprintf(stderr, "No benchmark selected.\n");
    // TODO: list benchmarks.
    exit(EXIT_FAILURE);
  }

  if (policy >= NR_POLICIES) {
    fprintf(stderr, "No valid policy selected.\n");
    exit(EXIT_FAILURE);
  }

  hwloc_const_cpuset_t global = hwloc_topology_get_topology_cpuset(topology);
  if (!hwloc_bitmap_isincluded(cpuset1, global)) {
    fprintf(stderr, "cpuset-1 is not a subset of the complete cpuset.\n");
    char *set1str, *setstr;
    hwloc_bitmap_list_asprintf(&set1str, cpuset1);
    hwloc_bitmap_list_asprintf(&setstr, global);
    fprintf(stderr, "cpuset-1: %s\n", set1str);
    fprintf(stderr, "global:   %s\n", setstr);
    free(setstr);
    free(set1str);
    exit(EXIT_FAILURE);
  }

  if (!hwloc_bitmap_isincluded(cpuset2, global)) {
    fprintf(stderr, "cpuset-2 is not a subset of the complete cpuset.\n");
    char *set2str, *setstr;
    hwloc_bitmap_list_asprintf(&set2str, cpuset2);
    hwloc_bitmap_list_asprintf(&setstr, global);
    fprintf(stderr, "cpuset-2: %s\n", set2str);
    fprintf(stderr, "global:   %s\n", setstr);
    free(setstr);
    free(set2str);
    exit(EXIT_FAILURE);
  }

  init_benchmarks(argc, argv);

  threads_t *workers = spawn_workers(topology);

  if (policy == PAIR) {
    hwloc_bitmap_or(runset, cpuset1, cpuset2);
  } else {
    hwloc_bitmap_or(runset, runset, workers->cpuset);
  }

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];

    fprintf(stdout, "Running %s\n", benchmark->name);

    benchmark_result_t result;
    switch (policy) {
    case PARALLEL:
      result = run_in_parallel(workers, benchmark, iterations);
      break;
    case ONE_BY_ONE:
      result = run_one_by_one(workers, benchmark, iterations);
      break;
    case PAIR:
      result = run_two_benchmarks(workers, benchmarks[i], benchmarks[i + 1],
                                  cpuset1, cpuset2, iterations);
      i = i + 1;
      break;
    case NR_POLICIES:
      exit(EXIT_FAILURE);
    }

    result_print(output, workers, result, runset);
  }

  stop_workers(workers);
}
