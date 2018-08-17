#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <vector>
#include <string>

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
#include <config.h>
#include "benchmark.h"

#include "runner.h"
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

static void tune_benchmarks_time(std::vector<benchmark_t *> benchmarks,
                                 std::vector<std::string> &argv,
                                 const double time,
                                 const benchmark_config_t *const config) {
  const unsigned rounds = 10;
  const auto offset = argv.size();

  for (const auto &benchmark : benchmarks) {
    using namespace std::string_literals;
    argv.push_back("--"s + benchmark->name +
                   "-rounds=" + std::to_string(rounds));
  }

  std::vector<const char *> argv_;
  for (const auto &arg : argv) {
    argv_.push_back(arg.c_str());
  }

  init_benchmarks(static_cast<int>(argv.size()),
                  const_cast<char **>(argv_.data()), config);

  argv.resize(offset);
  for (const auto &benchmark : benchmarks) {
    using namespace std::string_literals;
    argv.push_back("--"s + benchmark->name + "-rounds=" +
                   std::to_string(tune_time(benchmark, time, rounds)));
  }

  std::cerr << "[Time] ";
  for (const auto &arg : argv) {
    std::cerr << arg << ' ';
  }
  std::cerr << std::endl;
}

static int file_exists(const char *name) {
  struct stat tmp;
  int err = stat(name, &tmp);
  return (err == 0) || (errno != ENOENT);
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
  std::unique_ptr<hwloc::topology> topology = std::make_unique<hwloc::topology>();
  topology->load();

  struct hwloc_obj_attr_u::hwloc_cache_attr_s l1 = l1_attributes(topology->get());

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
  hwloc::cpuset cpuset1;
  hwloc::cpuset cpuset2;

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
      cpuset1 = hwloc::bitmap(optarg, hwloc::bitmap::list_tag{});
      break;
    case 2:
      cpuset2 = hwloc::bitmap(optarg, hwloc::bitmap::list_tag{});
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
    const bool thissystem = topology->is_thissystem();
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

  std::vector<benchmark_t *> benchmarks;

  if (opt_benchmarks != NULL) {
    const unsigned num_benchmarks = number_benchmarks();
    char *arg = strtok(opt_benchmarks, ",");
    unsigned i = 0;
    for (i = 0; arg != NULL && i < num_benchmarks; ++i) {
      benchmark_t * benchmark = get_benchmark_name(arg);
      if (benchmark == NULL) {
        fprintf(stderr, "Benchmark %s unknown. Skipping.\n", arg);
        --i;
      } else {
        benchmarks.push_back(benchmark);
      }
      arg = strtok(NULL, ",");
    }
  } else {
    const unsigned num_benchmarks = number_benchmarks();
    for (unsigned i = 0; i < num_benchmarks; ++i) {
      benchmarks.push_back(get_benchmark_idx(i));
    }
  }

  pmc pmcs;
  if (opt_pmcs != nullptr) {
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
    std::cerr << "Tuning rounds parameter:\n";
    std::vector<std::string> my_argv(1, "");
    tune_benchmarks_time(benchmarks, my_argv, time, &config);
    exit(EXIT_SUCCESS);
  }

  if (auto_tune) {
    /* alloc space for input argv + *-rounds= parameters */
    std::vector<std::string> my_argv;
    std::copy_n(argv, argc, std::back_inserter(my_argv));

    tune_benchmarks_time(benchmarks, my_argv, time, &config);
    config.verbose = 0;

    std::vector<const char *> argv_;
    for (const auto &arg : my_argv) {
      argv_.push_back(arg.c_str());
    }

    init_benchmarks(static_cast<int>(argv_.size()),
                    const_cast<char **>(argv_.data()), &config);
  } else {
    init_benchmarks(argc, argv, &config);
  }

  if (policy >= NR_POLICIES) {
    fprintf(stderr, "No valid policy selected.\n");
    exit(EXIT_FAILURE);
  }

  const hwloc::bitmap global = topology->get_topology_cpuset();

  /* default is the whole machine */
  if (!cpuset1) {
    cpuset1 = global;
  }

  if (!cpuset1.isincluded(global)) {
    std::cerr << "cpuset-1 is not a subset of the complete cpuset.\n";
    std::cerr << "cpuset-1: " << cpuset1 << "\n";
    std::cerr << "global:   " << global << " \n";
    exit(EXIT_FAILURE);
  }

  if (!cpuset2.isincluded(global)) {
    std::cerr << "cpuset-2 is not a subset of the complete cpuset.\n";
    std::cerr << "cpuset-2: " << cpuset2 << "\n";
    std::cerr << "global:   " << global << " \n";
    exit(EXIT_FAILURE);
  }

  assert((policy == PAIR) == static_cast<bool>(cpuset2));
  auto runset = cpuset1 | cpuset2;

  runner b_runner(topology.get(), runset, use_hyperthreads, do_binding);

  for (auto benchmark = benchmarks.cbegin(); benchmark != benchmarks.cend();
       ++benchmark) {
    std::cout << "# " << (*benchmark)->name << std::endl;

    std::unique_ptr<benchmark_result> result;
    switch (policy) {
    case PARALLEL:
      result =
          b_runner.parallel(*benchmark, iterations, pmcs);
      break;
    case ONE_BY_ONE:
      result =
          b_runner.serial(*benchmark, iterations, pmcs);
      break;
    case PAIR: {
      const auto next = std::next(benchmark);
      result = b_runner.parallel(
          *benchmark, (next == benchmarks.end()) ? *benchmark : *next, cpuset1,
          cpuset2, iterations, pmcs);
      ++benchmark;
    } break;
    case NR_POLICIES:
      exit(EXIT_FAILURE);
    }

    *output << *result;
  }
}
