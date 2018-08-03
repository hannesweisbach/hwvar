#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>

#include <ctype.h>
#include <getopt.h>
#include <inttypes.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <time.h>
#include <cmath>

#include <gsl/gsl>

#include "hwloc"
#include <platform.h>
#include <worker.h>
#include <config.h>
#include "benchmark.h"

#include "dgemm.h"
#include "streambuffer.h"

#include "pmcs.h"

static uint64_t get_time() {
#ifdef HAVE_CLOCK_GETTIME
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
    perror("clock_gettime() failed");
    exit(EXIT_FAILURE);
  }

  return (uint64_t)ts.tv_sec * 1000 * 1000 * 1000 + (uint64_t)ts.tv_nsec;
#else
  struct timeval tv;
  if (gettimeofday(&tv, NULL)) {
    perror("gettimeofday() failed");
    exit(EXIT_FAILURE);
  }

  return (uint64_t)tv.tv_sec * 1000 * 1000 * 1000 + (uint64_t)tv.tv_usec * 1000;
#endif
}

static threads_t *spawn_workers(hwloc_topology_t topology,
                                hwloc_const_cpuset_t cpuset,
                                int include_hyperthreads,
                                int do_binding) {
  const hwloc_obj_type_t type =
      (include_hyperthreads) ? HWLOC_OBJ_PU : HWLOC_OBJ_CORE;
  const int depth = hwloc_get_type_or_below_depth(topology, type);
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
    hwloc_obj_t obj = hwloc_get_obj_by_type(topology, type, pu);
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
    const int cpunum_check = hwloc_bitmap_first(tmp);
    if (cpunum_check < 0) {
      fprintf(stderr, "No index is set in the bitmask\n");
      exit(EXIT_FAILURE);
    }
    const unsigned cpunum = (unsigned)cpunum_check;

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
    thread->thread_arg.topology = topology;
    thread->thread_arg.cpu = cpunum;
    thread->thread_arg.cpuset = tmp;
    thread->thread_arg.do_binding = do_binding;
    hwloc_bitmap_asprintf(&thread->thread_arg.cpuset_string, tmp);
#if 0
    fprintf(stderr, "Found L:%u P:%u %s %d %d %d\n", obj->logical_index,
            obj->os_index, thread->thread_arg.cpuset_string, i, pu,
            cpunum);
#endif

    hwloc_bitmap_set(allocated, cpunum);
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

static struct hwloc_obj_attr_u::hwloc_cache_attr_s l1_attributes(hwloc_topology_t topology) {
  const int depth = hwloc_get_type_or_below_depth(topology, HWLOC_OBJ_PU);
  if (depth < 0) {
    fprintf(stderr, "Error finding PU\n");
    exit(EXIT_FAILURE);
  }
  hwloc_obj_t obj = hwloc_get_obj_by_depth(topology, (unsigned)depth, 0);
  /* Discover cache line size */
  hwloc_obj_t cache = hwloc_get_cache_covering_cpuset(topology, obj->cpuset);
  assert(cache != NULL);
#if HWLOC_API_VERSION >= 0x00020001
  assert(cache->type == HWLOC_OBJ_L1CACHE);
#else
  assert(cache->type == HWLOC_OBJ_CACHE);
#endif
  assert(cache->attr->cache.depth == 1);
  fprintf(stderr, "[L1] size: %" PRIu64 ", line size: %u\n",
          cache->attr->cache.size, cache->attr->cache.linesize);

  return cache->attr->cache;
}

class benchmark_result {
  std::unique_ptr<hwloc::cpuset> cpus_;
  const pmc *pmcs_;
  unsigned repetitions_;
  std::vector<uint64_t> data_;

public:
  benchmark_result(const hwloc::cpuset &cpus, const pmc &pmcs,
                   const unsigned repetitions)
      : cpus_(std::make_unique<hwloc::cpuset>(cpus)), pmcs_(&pmcs),
        repetitions_(repetitions),
        data_(cpus_->size() * pmcs_->size() * repetitions_) {}

  gsl::span<uint64_t> buffer_for_thread(const int i) {
    auto span = gsl::span<uint64_t>(data_);
    return span.subspan(i * repetitions_ * pmcs_->size(),
                        repetitions_ * pmcs_->size());
  }

  friend std::ostream &operator<<(std::ostream &os,
                                  const benchmark_result &result) {
    for (const auto &pmc : *result.pmcs_) {
      os << "# " << pmc.name() << '\n';
      for (const auto cpu : *result.cpus_) {
        const auto logical = cpu.first;
        const auto physical = cpu.second;
        os << std::setw(3) << physical << ' ';
        for (unsigned rep = 0; rep < result.repetitions_; ++rep) {
          os << std::setw(10)
             << result
                    .data_[logical * result.repetitions_ * result.pmcs_->size() +
                           result.pmcs_->size() * rep + pmc.offset()]
             << ' ';
        }
        os << '\n';
      }
    }

    return os;
  }
};

#include <benchmark.h>

static std::unique_ptr<benchmark_result>
run_in_parallel(threads_t *workers, benchmark_t *ops,
                const unsigned repetitions, const pmc &pmcs);

static void *null_thread(void *arg_) { return arg_; }

static void synchronize_worker_init(threads_t *threads) {
  benchmark_t null_ops = {"null", NULL, NULL, NULL, NULL, null_thread, NULL};

  run_in_parallel(threads, &null_ops, 1, pmc{});
}

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
  step->work->pmcs = NULL;

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

  free_step(step);

  if (!err) {
    // TODO cleanup.
  }
}

static std::unique_ptr<benchmark_result>
run_in_parallel(threads_t *workers, benchmark_t *ops,
                const unsigned repetitions, const pmc &pmcs) {
  const int cpus = hwloc_bitmap_weight(workers->cpuset);
  auto result =
      std::make_unique<benchmark_result>(workers->cpuset, pmcs, repetitions);
  step_t *step = init_step(cpus);

  for (int i = 0; i < cpus; ++i) {
    work_t *work = &step->work[i];
    work->ops = ops;
    work->arg = ops->state;
    work->barrier = &step->barrier;
    work->results = result->buffer_for_thread(i);
    work->reps = repetitions;
    work->pmcs = &pmcs;

    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
  }

  // run dirigent
  assert(workers->threads[0].thread_arg.dirigent);
  worker(&workers->threads[0].thread_arg);
  for (int i = 0; i < cpus; ++i) {
    wait_until_done(&workers->threads[i].thread_arg);
  }

  free_step(step);

  return result;
}

static std::unique_ptr<benchmark_result>
run_one_by_one(threads_t *workers, benchmark_t *ops, const unsigned repetitions,
               const pmc &pmcs) {
  const int cpus = hwloc_bitmap_weight(workers->cpuset);
  auto result =
      std::make_unique<benchmark_result>(workers->cpuset, pmcs, repetitions);
  step_t *step = init_step(1);

  uint64_t diff = 0;
  for (int i = 0; i < cpus; ++i) {
    work_t *work = &step->work[0];
    work->ops = ops;
    work->arg = ops->state;
    work->barrier = &step->barrier;
    work->results = result->buffer_for_thread(i);
    work->reps = repetitions;
    work->pmcs = &pmcs;

    if (i) {
      const unsigned secs = (unsigned)(diff / (1000 * 1000 * 1000UL));
      const unsigned sec = secs % 60UL;
      const unsigned mins = (secs - sec) / 60;
      const unsigned min = mins % 60UL;
      const unsigned hours = (mins - min) / 60;
      fprintf(stderr,
              "Running %u of %i. Last took %02u:%02u:%02u (%us)\r",
              i + 1, cpus, hours, min, sec, secs);
      fflush(stderr);
    } else {
      fprintf(stderr, "Running %u of %i\r", i + 1, cpus);
      fflush(stderr);
    }

    const uint64_t begin = get_time();
    struct arg *arg = &workers->threads[i].thread_arg;
    queue_work(arg, work);
    if (arg->dirigent) { // run dirigent
      assert(i == 0);
      worker(arg);
    }
    wait_until_done(arg);
    diff = get_time() - begin;
  }

  free_step(step);

  return result;
}

static std::unique_ptr<benchmark_result>
run_two_benchmarks(threads_t *workers, benchmark_t *ops1, benchmark_t *ops2,
                   hwloc_const_cpuset_t set1, hwloc_const_cpuset_t set2,
                   const unsigned repetitions, const pmc &pmcs) {
  assert(!hwloc_bitmap_intersects(set1, set2));
  hwloc_cpuset_t cpuset = hwloc_bitmap_alloc();
  hwloc_bitmap_or(cpuset, set1, set2);
  hwloc_bitmap_and(cpuset, cpuset, workers->cpuset);
  assert(hwloc_bitmap_isincluded(cpuset, workers->cpuset));
  const int cpus = hwloc_bitmap_weight(cpuset);
  assert(cpus > 0);
  auto result = std::make_unique<benchmark_result>(cpuset, pmcs, repetitions);
  step_t *step = init_step(cpus);

  for (int i = 0; i < cpus; ++i) {
    struct arg *arg = &workers->threads[i].thread_arg;
    work_t *work = &step->work[i];
    work->ops = hwloc_bitmap_isset(set1, arg->cpu) ? ops1 : ops2;
    work->arg = hwloc_bitmap_isset(set1, arg->cpu) ? ops1->state : ops2->state;
    work->barrier = &step->barrier;
    work->results = result->buffer_for_thread(i);
    work->reps = repetitions;
    work->pmcs = &pmcs;

    queue_work(arg, work);
  }

  // run dirigent
  assert(workers->threads[0].thread_arg.dirigent);
  worker(&workers->threads[0].thread_arg);
  for (int i = 1; i < cpus; ++i) {
    wait_until_done(&workers->threads[i].thread_arg);
  }

  hwloc_bitmap_free(cpuset);
  free_step(step);

  return result;
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
static unsigned tune_time(benchmark_t *benchmark, const double target_seconds,
                          const unsigned init_rounds) {
  void *benchmark_arg =
      (benchmark->init_arg) ? benchmark->init_arg(benchmark->state) : NULL;
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
  unsigned ret = (unsigned)rounds_i;
  if (ret < 1) {
    ret = 1;
  }
  const double runtime = ret / rep * duration / 1e9 / init_rounds;

  fprintf(stderr, "[Time] --%s-rounds=%u (~%4.1fs)\n", benchmark->name, ret,
          runtime);

  return ret;
}

static void tune_benchmarks_time(benchmark_t **benchmarks,
                                 const unsigned num_benchmarks, char *argv[],
                                 const unsigned argc, const double time,
                                 const benchmark_config_t *const config) {
  const unsigned rounds = 10;
  for (unsigned i = 0; i < num_benchmarks; ++i) {
    asprintf(&argv[argc + i], "--%s-rounds=%u", benchmarks[i]->name, rounds);
  }

  init_benchmarks((int)(argc + num_benchmarks), argv, config);

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

static int file_exists(const char *name) {
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

double parse_double(const char *optarg, const char *name, int positive) {
  errno = 0;
  char *suffix = NULL;
  const double value = strtod(optarg, &suffix);
  if (errno == ERANGE || std::isnan(value) || std::isinf(value)) {
    fprintf(stderr, "Could not parse --%s argument '%s': %s\n", name, optarg,
            strerror(errno));
    exit(EXIT_FAILURE);
  }

  if (positive && value < 0.0) {
    fprintf(stderr, "--%s argument must be positive", name);
    exit(EXIT_FAILURE);
  }

  return value;
}

int main(int argc, char *argv[]) {
  hwloc_topology_t topology = NULL;
  if (hwloc_topology_init(&topology)) {
    printf("hwloc_topology_init failed\n");
    exit(EXIT_FAILURE);
  }
  if (hwloc_topology_load(topology)) {
    printf("hwloc_topology_load() failed\n");
    exit(EXIT_FAILURE);
  }

  struct hwloc_obj_attr_u::hwloc_cache_attr_s l1 = l1_attributes(topology);

  enum policy { PARALLEL, ONE_BY_ONE, PAIR, NR_POLICIES };

  enum policy policy = ONE_BY_ONE;
  char *opt_benchmarks = NULL;
  char *opt_pmcs = NULL;
  unsigned iterations = 13;
  uint64_t size = l1.size;
  double fill = 0.9;
  double time = 20;
  std::ostream *output = &std::cout;
  std::ostream *tee;
  std::ofstream file;
  static int auto_tune = 0;
  static int tune = 0;
  static int use_hyperthreads = 1;
  static int do_binding = 1;
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
      {"fill", required_argument, NULL, 'f'},
      {"time", required_argument, NULL, 't'},
      {"tune", no_argument, &tune, 1},
      {"auto", no_argument, &auto_tune, 1},
      {"no-ht", no_argument, &use_hyperthreads, 0},
      {"disable-binding", no_argument, &do_binding, 0},
      {"pmcs", required_argument, NULL, 'm'},
      {NULL, 0, NULL, 0}};

  opterr = 0;

  while (1) {
    int c = getopt_long(argc, argv, "p:b:i:lo:s:t:", options, NULL);
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
      } else if ((strcmp(optarg, "onebyone") == 0) ||
                 (strcmp(optarg, "one-by-one") == 0)) {
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
    case 'm':
      opt_pmcs = optarg;
      break;
    case 'o':
      if (strcmp(optarg, "-") == 0) {
        /* stdout is the default */
      } else if (file_exists(optarg)) {
        fprintf(stderr, "File %s already exists.\n", optarg);
        exit(EXIT_FAILURE);
      } else {
        file.open(optarg);
        output = &file;
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
    case 'f':
      fill = parse_double(optarg, "fill", 1);
      break;
    case 't':
      time = parse_double(optarg, "time", 1);
      break;
    case ':':
      break;
    default:
      break;
    }
  }

  {
    const int thissystem = hwloc_topology_is_thissystem(topology);
    fprintf(stderr, "Topology is from this system: %s",
            thissystem ? "yes" : "no");
    if (!thissystem && do_binding) {
      fprintf(stderr, "; hwloc will not bind threads.");
#ifdef HAVED_SCHED_H
      fprintf(stderr, "; falling back to sched_setaffinity().");
#endif
    } else if (!do_binding) {
      fprintf(stderr, "; explicit thread binding disabled.");
    }
    fprintf(stderr, "\n");
  }

#ifdef L4
  const std::size_t sz = 1024 * 1024;
  char *const buf = new char[sz];
  memset(buf, 0, sz);
  shm_buffer strbuf(buf, sz);
  std::ostream shm(&strbuf);
  output = &shm;
#endif

#ifndef L4
  if (output == &file) {
#endif
    tee = new teestream(std::cout, file);
    output = tee;
#ifndef L4
  }
#endif

  unsigned num_benchmarks = opt_benchmarks == NULL
                                ? number_benchmarks()
                                : count_chars(opt_benchmarks, ',') + 1;

  benchmark_t **benchmarks =
      (benchmark_t **)malloc(sizeof(benchmark_t *) * num_benchmarks);
  if (benchmarks == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    exit(EXIT_FAILURE);
  }

  if (opt_benchmarks != NULL) {
    char *arg = strtok(opt_benchmarks, ",");
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

  pmc pmcs;
  {
    std::stringstream ss(opt_pmcs);
    std::string token;
    while (std::getline(ss, token, ',')) {
      try {
        pmcs.add(token);
      } catch (const std::exception &e) {
        std::cerr << "Error resolving PMC \"" << token << "\": " << e.what()
                  << std::endl;
      }
    }
  }

  benchmark_config_t config = {size, fill, l1.linesize, 1};

  if (tune) {
    const unsigned num_args = num_benchmarks + 1;
    char **myargv = (char **)malloc(sizeof(char *) * num_args);
    myargv[0] = ""; // the first arg for getopt to skip over.
    fprintf(stderr, "Tuning rounds parameter:\n");
    tune_benchmarks_time(benchmarks, num_benchmarks, myargv, 1,
                         time, &config);
    for (unsigned i = 1; i < num_args; ++i) {
      free(myargv[i]);
    }
    free(myargv);
    exit(EXIT_SUCCESS);
  }

  if (auto_tune) {
    /* alloc space for input argv + *-rounds= parameters */
    unsigned idx = (unsigned)argc;
    const unsigned num_args = num_benchmarks + idx;
    char **myargv = (char **)malloc(sizeof(char *) * num_args);
    memcpy(myargv, argv, idx * sizeof(char *));

    tune_benchmarks_time(benchmarks, num_benchmarks, myargv, idx, time,
                         &config);
    config.verbose = 0;
    init_benchmarks((int)(idx + num_benchmarks), myargv, &config);
    for (unsigned i = (unsigned)argc; i < num_args; ++i) {
      free(myargv[i]);
    }
    free(myargv);
  } else {
    init_benchmarks(argc, argv, &config);
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

  threads_t *workers = spawn_workers(topology, runset,
          use_hyperthreads, do_binding);

  synchronize_worker_init(workers);

  for (unsigned i = 0; i < num_benchmarks; ++i) {
    benchmark_t *benchmark = benchmarks[i];

    fprintf(stdout, "# %s\n", benchmark->name);

    std::unique_ptr<benchmark_result> result;
    switch (policy) {
    case PARALLEL:
      result =
          run_in_parallel(workers, benchmark, iterations, pmcs);
      break;
    case ONE_BY_ONE:
      result =
          run_one_by_one(workers, benchmark, iterations, pmcs);
      break;
    case PAIR: {
      const unsigned next = i + 1 < num_benchmarks ? i + 1 : i;
      result =
          run_two_benchmarks(workers, benchmarks[i], benchmarks[next], cpuset1,
                             cpuset2, iterations, pmcs);
      i = i + 1;
    } break;
    case NR_POLICIES:
      exit(EXIT_FAILURE);
    }

    *output << *result;
  }

  stop_workers(workers);
}
