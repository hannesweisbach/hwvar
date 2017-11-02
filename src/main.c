#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

#include <hwloc.h>

#include <platform.h>
#include <worker.h>
#include "benchmark.h"

#include "dgemm.h"

static threads_t *spawn_workers(hwloc_topology_t topology,
                                hwloc_const_cpuset_t cpuset) {
  const int depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_PU);
  const unsigned num_threads =
      hwloc_get_nbobjs_by_depth(topology, (unsigned)depth);

  threads_t *workers = (threads_t *)malloc(sizeof(threads_t));
  if (workers == NULL) {
    exit(EXIT_FAILURE);
  }

  char *str;
  hwloc_bitmap_asprintf(&str, cpuset);
  fprintf(stderr, "Spawning %d workers: %s\n", hwloc_bitmap_weight(cpuset), str);
  free(str);

  workers->threads =
      (thread_data_t *)malloc(sizeof(thread_data_t) * num_threads);
  if (workers->threads == NULL) {
    exit(EXIT_FAILURE);
  }

  // iterate over all cores and pick the ones in the cpuset
  hwloc_cpuset_t allocated = hwloc_bitmap_alloc();
  for (unsigned pu = 0, i = 0; pu < num_threads; ++pu) {
    /* TODO Awesome:
     * If no object for that type exists, NULL is returned. If there
     * are several levels with objects of that type, NULL is returned and ther
     * caller may fallback to hwloc_get_obj_by_depth(). */
    hwloc_obj_t obj = hwloc_get_obj_by_type(topology, HWLOC_OBJ_PU, pu);
    if (obj == NULL) {
      printf("Error getting obj. Implement fallback to "
             "hwloc_get_obj_by_depth()?\n");
      exit(EXIT_FAILURE);
    }

    hwloc_cpuset_t tmp = hwloc_bitmap_dup(obj->cpuset);
    // TODO might this get the same mask for two sets?!
    hwloc_bitmap_singlify(tmp);
    if (!hwloc_bitmap_isincluded(tmp, cpuset)) {
      hwloc_bitmap_free(tmp);
      continue;
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
    thread->thread_arg.cpuset = tmp;
    hwloc_bitmap_asprintf(&thread->thread_arg.cpuset_string, tmp);
#if 0
    fprintf(stderr, "Found L:%u P:%u %s %d\n", obj->logical_index, obj->os_index,
            thread->thread_arg.cpuset_string, i);
#endif

    hwloc_bitmap_set(allocated, obj->os_index);
    pthread_mutex_unlock(&thread->thread_arg.lock);

    if (i > 0) {
      int err =
          pthread_create(&thread->thread, NULL, worker, &thread->thread_arg);
      if (err) {
        fprintf(stderr, "Error creating thread.\n");
        // can't cancel threads; just quit.
        return NULL;
      }
    }

    i = i + 1;
  }

  assert(hwloc_bitmap_weight(allocated) == hwloc_bitmap_weight(cpuset));

  workers->cpuset = allocated;

  return workers;
}

static struct hwloc_cache_attr_s l1_attributes(hwloc_topology_t topology) {
  const int depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_PU);
  if (depth < 0) {
    fprintf(stderr, "Error finding PU\n");
    exit(EXIT_FAILURE);
  }
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology, (unsigned)depth, 0);
  /* Discover cache line size */
  hwloc_obj_t cache = hwloc_get_cache_covering_cpuset(topology, obj->cpuset);
  assert(cache != NULL);
  assert(cache->type == HWLOC_OBJ_CACHE);
  assert(cache->attr->cache.depth == 1);
  fprintf(stderr, "[L1] size: %" PRIu64 ", line size: %u\n",
          cache->attr->cache.size, cache->attr->cache.linesize);

  return cache->attr->cache;
}

#include <benchmark.h>

static void *stop_thread(void *arg_) {
  struct arg *arg = (struct arg *)arg_;
  arg->run = 0;
  return NULL;
}

static int stop_single_worker(thread_data_t *thread, step_t *step) {
  benchmark_t stop_ops = {"stop", NULL,        NULL, NULL,
                          NULL,   stop_thread, NULL, {0, 0, 0}};

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

  int err = 0;
  for (int i = 0; i < hwloc_bitmap_weight(workers->cpuset); ++i) {
    if (workers->threads[i].thread_arg.dirigent) { // skip dirigent
      continue;
    }
    err |= stop_single_worker(&workers->threads[i], step);
  }

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

static void result_print(FILE *file, benchmark_result_t result,
                         hwloc_const_cpuset_t cpuset) {
  int cpu = -1;
  for (unsigned thread = 0; thread < result.threads; ++thread) {
    cpu = hwloc_bitmap_next(cpuset, cpu);
    fprintf(file, "%2d ", cpu);
    for (unsigned rep = 0; rep < result.repetitions; ++rep) {
      fprintf(file, "%10" PRIu64 " ", result.data[thread * result.repetitions + rep]);
    }
    fprintf(file, "\n");
  }
}

static benchmark_result_t run_in_parallel(threads_t *workers, benchmark_t *ops,
                                          const unsigned repetitions) {
  const int cpus = hwloc_bitmap_weight(workers->cpuset);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(cpus);

  for (int i = 0; i < cpus; ++i) {
    work_t *work = &step->work[i];
    work->ops = ops;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[(unsigned)i * repetitions];
    work->reps = repetitions;

    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
  }

  // run dirigent
  assert(workers->threads[0].thread_arg.dirigent);
  worker(&workers->threads[0].thread_arg);
  for (int i = 1; i < cpus; ++i) {
    wait_until_done(&workers->threads[i].thread_arg);
  }

  return result;
}

static benchmark_result_t run_one_by_one(threads_t *workers, benchmark_t *ops,
                                         unsigned repetitions) {
  const int cpus = hwloc_bitmap_weight(workers->cpuset);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(1);

  for (int i = 0; i < cpus; ++i) {
    work_t *work = &step->work[0];
    work->ops = ops;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[(unsigned)i * repetitions];
    work->reps = repetitions;

    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
    if (arg->dirigent) { // run dirigent
      assert(i == 0);
      worker(arg);
    }
    wait_until_done(arg);
  }

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
  const int cpus = hwloc_bitmap_weight(cpuset);
  assert(cpus > 0);
  benchmark_result_t result = result_alloc((unsigned)cpus, repetitions);
  step_t *step = init_step(cpus);

  for (int i = 0; i < cpus; ++i) {
    struct arg *arg = &workers->threads[i].thread_arg;
    work_t *work = &step->work[i];
    work->ops = hwloc_bitmap_isset(set1, arg->cpu) ? ops1 : ops2;
    work->arg = NULL;
    work->barrier = &step->barrier;
    work->result = &result.data[(unsigned)i * repetitions];
    work->reps = repetitions;

    queue_work(arg, work);
  }

  // run dirigent
  assert(workers->threads[0].thread_arg.dirigent);
  worker(&workers->threads[0].thread_arg);
  for (int i = 1; i < cpus; ++i) {
    wait_until_done(&workers->threads[i].thread_arg);
  }

  hwloc_bitmap_free(cpuset);

  return result;
}

/**
 * Calculate input size of a benchmark.
 *
 * Calculates the input size of a benchmark, such that the cache is 90% full,
 * with the intention of leaving some space for stack, etc … in the cache. At
 * the same time, the routine calculates the benchmark size such that an integer
 * number of cache lines are used.
 * As a diagnostic the calculated number of bytes and the fill level of the
 * cache are printed.
 *
 * @param name name of the benchmark
 * @param cache_size  total cache size
 * @param cache_line_size size of a cache line
 * @param power power with which the size argument goes into the memory
 *              requirement of the benchmark, i.e. 1 for vectors, 2 for square
 *              matrices.
 * @param data_size sizeof() of the data type used by the benchmark.
 * @param datasets number of datasets used by the benchmark, i.e. number of
 *                  vectors, matrices, …
 **/
static unsigned tune_size(const char *const name, const uint64_t cache_size,
                          const unsigned cache_line_size,
                          const unsigned data_size, const uint16_t power,
                          const uint16_t datasets) {
  const double cache_size_ = cache_size * 0.9;
  const double elems_per_set = cache_size_ / datasets / data_size;
  const double elems_per_dim = pow(elems_per_set, 1.0 / power);
  const unsigned num_lines =
      ((unsigned)elems_per_dim / (cache_line_size / data_size));
  const unsigned bytes_per_dim_aligned = num_lines * cache_line_size;
  const unsigned n = bytes_per_dim_aligned / data_size;
  const double bytes = pow(n, power) * data_size * datasets;
  const double p = bytes * 100.0 / cache_size;

  fprintf(stderr, "[Cache] --%s-size=%u requires %u bytes of %"PRIu64"k (%5.1f%%)\n",
          name, n, (unsigned)bytes, cache_size / 1024, p);
  return n;
}

static uint64_t get_time() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC_RAW, &ts);

  return (uint64_t)ts.tv_sec * 1000 * 1000 * 1000 + (uint64_t)ts.tv_nsec;
}

/**
 * Tune the rounds parameter such that a pre-defined benchmark runtime is
 * achieved.
 *
 * Assumes, that the benchmark is configured for the desired size.
 *
 * @param benchmark the benchmark to tune
 * @param target_seconds the target runtime in seconds
 * @param init_rounds the rounds-parameter the benchmark was initialized with
 **/
static unsigned tune_time(benchmark_t *benchmark, const uint64_t target_seconds,
                          const unsigned init_rounds) {
  void *benchmark_arg =
      (benchmark->init_arg) ? benchmark->init_arg(NULL) : NULL;
  const uint64_t start = get_time();
  unsigned rep;
  for (rep = 0; get_time() < start + 1000 * 1000 * 1000UL; ++rep) {
    if (benchmark->reset_arg) {
      benchmark->reset_arg(benchmark_arg);
    }
    benchmark->call(benchmark_arg);
  }
  const uint64_t end = get_time();
  const uint64_t duration = end - start;

  const double rounds = (target_seconds * 1e9 * rep * init_rounds) / duration;
  const double rounds_i = nearbyint(rounds);
  assert(rounds <= UINT_MAX);
  const double runtime = rounds_i / rep * duration / 1e9 / init_rounds;

  fprintf(stderr, "[Time] --%s-rounds=%u (~%4.1fs)\n", benchmark->name,
          (unsigned)rounds, runtime);

  return (unsigned)rounds;
}

static void tune_benchmarks_size(benchmark_t **benchmarks,
                                 const unsigned num_benchmarks,
                                 const uint64_t cache_size,
                                 const unsigned cache_line_size, char **argv) {
  for (unsigned i = 0; i < num_benchmarks; ++i) {
    tuning_param_t *p = &benchmarks[i]->params;
    const char *name = benchmarks[i]->name;
    const unsigned n = tune_size(name, cache_size, cache_line_size,
                                 p->data_size, p->power, p->datasets);
    asprintf(&argv[i], "--%s-size=%u ", name, n);
  }

  fprintf(stderr, "[Cache] ");
  for (unsigned i = 0; i < num_benchmarks; ++i) {
    fprintf(stderr, "%s ", argv[i]);
  }
  fprintf(stderr, "\n");
}

static void tune_benchmarks_time(benchmark_t **benchmarks,
                                 const unsigned num_benchmarks,
                                 const uint64_t time, char **argv,
                                 unsigned argc) {
  const unsigned rounds = 10;
  for (unsigned i = 0; i < num_benchmarks; ++i) {
    asprintf(&argv[argc + i], "--%s-rounds=%u", benchmarks[i]->name, rounds);
  }

  init_benchmarks((int)(argc + num_benchmarks), argv);

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    free(argv[argc + i]);
    asprintf(&argv[argc + i], "--%s-rounds=%u", benchmarks[i]->name,
             tune_time(benchmarks[i], time, rounds));
  }

  fprintf(stderr, "[Time] ");
  for (unsigned i = 0; i < num_benchmarks; ++i) {
    fprintf(stderr, "%s ", argv[argc + i]);
  }
  fprintf(stderr, "\n");
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

static unsigned si_suffix_to_factor(int suffix) {
  switch (tolower(suffix)) {
  case '\0':
    return 1;
  case 'k':
    return 1024;
  case 'm':
    return 1024 * 1024;
  case 'g':
    return 1024 * 1024 * 1024;
  default:
    fprintf(stderr, "'%c' is not a valid suffix.\n", suffix);
    exit(EXIT_FAILURE);
  }
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

  struct hwloc_cache_attr_s l1 = l1_attributes(topology);

  enum policy { PARALLEL, ONE_BY_ONE, PAIR, NR_POLICIES };

  enum policy policy = ONE_BY_ONE;
  char *opt_benchmarks = NULL;
  unsigned iterations = 10;
  uint64_t size = l1.size;
  uint64_t time = 20;
  FILE *output = stdout;
  static int auto_tune = 0;
  static int tune = 0;
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
      {"size", required_argument, NULL, 's'},
      {"time", required_argument, NULL, 't'},
      {"tune", no_argument, &tune, 1},
      {"auto", no_argument, &auto_tune, 1},
      {NULL, 0, NULL, 0}};

  opterr = 0;

  while (1) {
    int c = getopt_long(argc, argv, "p:b:i:lo:s:", options, NULL);
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
      opt_benchmarks = optarg;
      break;
    case 'i': {
      errno = 0;
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > UINT_MAX) {
        fprintf(stderr, "Could not parse --iterations argument '%s': %s\n",
                optarg, strerror(errno));
      }
      iterations = (unsigned)tmp;
    } break;
    case 'l':
      list_benchmarks();
      exit(EXIT_SUCCESS);
    case 'o':
      if (strcmp(optarg, "-") == 0) {
        /* stdout is the default */
      } else if (file_exists(optarg)) {
        fprintf(stderr, "File %s already exists.\n", optarg);
        exit(EXIT_FAILURE);
      } else {
        output = fopen(optarg, "w");
      }
      break;
    case 's': {
      errno = 0;
      char *suffix = NULL;
      unsigned long tmp = strtoul(optarg, &suffix, 0);
      if (errno == EINVAL || errno == ERANGE) {
        fprintf(stderr, "Could not parse --iterations argument '%s': %s\n",
                optarg, strerror(errno));
      }
      size = tmp;
      if (suffix != NULL) {
        size *= si_suffix_to_factor(*suffix);
      }
    } break;
    case 't': {
      errno = 0;
      char *suffix = NULL;
      unsigned long tmp = strtoul(optarg, &suffix, 0);
      if (errno == EINVAL || errno == ERANGE) {
        fprintf(stderr, "Could not parse --iterations argument '%s': %s\n",
                optarg, strerror(errno));
      }
      time = tmp;
    } break;
    case ':':
      break;
    default:
      break;
    }
  }

  unsigned num_benchmarks = opt_benchmarks == NULL
                                ? number_benchmarks()
                                : count_chars(optarg, ',') + 1;
  benchmark_t **benchmarks =
      (benchmark_t **)malloc(sizeof(benchmark_t *) * num_benchmarks);
  if (benchmarks == NULL) {
    fprintf(stderr, "Error allocating memory");
    exit(EXIT_FAILURE);
  }

  if (opt_benchmarks != NULL) {
    char *arg = strtok(optarg, ",");
    unsigned i = 0;
    for (i = 0; arg != NULL && i < num_benchmarks; ++i) {
      benchmarks[i] = get_benchmark_name(arg);
      if (benchmarks[i] == NULL) {
        fprintf(stderr, "Benchmark %s unknown. Skipping.\n", arg);
        --i;
      }
      arg = strtok(NULL, ",");
    }
    num_benchmarks = i;
  } else {
    for (unsigned i = 0; i < num_benchmarks; ++i) {
      benchmarks[i] = get_benchmark_idx(i);
    }
  }

  if (tune) {
    char **myargv = (char **)malloc(sizeof(char *) * (num_benchmarks * 2));
    fprintf(stderr, "Tuning size parameter:\n");
    tune_benchmarks_size(benchmarks, num_benchmarks, size, l1.linesize, myargv);
    fprintf(stderr, "Tuning rounds parameter:\n");
    tune_benchmarks_time(benchmarks, num_benchmarks, time, myargv,
                         num_benchmarks);
    for (unsigned i = 0; i < num_benchmarks * 2; ++i) {
      free(myargv[i]);
    }
    free(myargv);
    exit(EXIT_SUCCESS);
  }

  if (auto_tune) {
    /* alloc space for input argv + *-size= and *-rounds= parameters */
    unsigned idx = (unsigned)argc;
    char **myargv =
        (char **)malloc(sizeof(char *) * (num_benchmarks * 2 + idx));
    memcpy(myargv, argv, idx * sizeof(char *));
    tune_benchmarks_size(benchmarks, num_benchmarks, size, l1.linesize,
                         &myargv[idx]);
    idx += num_benchmarks;

    tune_benchmarks_time(benchmarks, num_benchmarks, time, myargv, idx);
    init_benchmarks((int)(idx + num_benchmarks), myargv);
  } else {
    init_benchmarks(argc, argv);
  }

  if (policy >= NR_POLICIES) {
    fprintf(stderr, "No valid policy selected.\n");
    exit(EXIT_FAILURE);
  }

  hwloc_const_cpuset_t global = hwloc_topology_get_topology_cpuset(topology);

  /* default is the whole machine */
  if (hwloc_bitmap_iszero(cpuset1)) {
    hwloc_bitmap_copy(cpuset1, global);
  }

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

  assert((policy == PAIR) == !hwloc_bitmap_iszero(cpuset2));
  hwloc_bitmap_or(runset, cpuset1, cpuset2);

  threads_t *workers = spawn_workers(topology, runset);

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
    case PAIR: {
      const unsigned next = i + 1 < num_benchmarks ? i + 1 : i;
      result = run_two_benchmarks(workers, benchmarks[i], benchmarks[next],
                                  cpuset1, cpuset2, iterations);
      i = i + 1;
    } break;
    case NR_POLICIES:
      exit(EXIT_FAILURE);
    }

    result_print(output, result, runset);
  }

  stop_workers(workers);
}
