#include <iostream>
#include <stdexcept>
#include <vector>

#include <cerrno>
#include <climits>
#include <cstdint>
#include <cstring>

#include <getopt.h>
#include <stdlib.h>
#include <unistd.h>

#include "ipi.h"

static int rounds = 10;
static void ipi_init(int argc, char *argv[],
                     const benchmark_config_t *const config) {
  static struct option longopts[] = {
      {"ipi-rounds", required_argument, NULL, 'r'}, {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        std::cerr << "Could not parse --syscall-rounds argument '" << optarg
                  << "': " << strerror(errno) << std::endl;
      }
      if (tmp <= 0) {
        std::cerr << "--ipi-rounds argument has to be positive.\n";
      }
      rounds = (int)tmp;
    } break;
    case ':':
    default:;
    }
  }

}

static void *ipi_arg_init(void *arg_) { return NULL; }
static void ipi_arg_free(void *arg_) {}

#define MODE_PINGPONG 0
#define MODE_PING     1
#define SKIP_LOCAL    2

static void *call_work(void *, const unsigned cpu, uint64_t *const data,
                       const unsigned count) {
  /* pass hwloc cpuset */
  /* exclude self */
  const auto err = syscall(852, cpu, data, static_cast<unsigned>(rounds), 0);
  if (err < 0) {
    throw std::runtime_error(strerror(errno));
  }
  return NULL;
}

benchmark_t ipi_ops = {
    .name = "ipi",
    .init = ipi_init,
    .init_arg = ipi_arg_init,
    .reset_arg = NULL,
    .free_arg = ipi_arg_free,
    .call = NULL,
    .extern_call = call_work,
    .state = NULL,
};

