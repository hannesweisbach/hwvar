#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "capacity.h"

static unsigned size;
static unsigned linesize;
static unsigned num_lines;
static unsigned rounds;

class capacity {
  uint8_t *data;

private:
  capacity(const capacity &);
  capacity &operator=(const capacity &);

public:
  capacity(const size_t size) : data(new uint8_t[size]) {}
  ~capacity() { delete[] data; }

  void work() {
    uint8_t tmp = 0;
    for (unsigned i = 0; i < num_lines; ++i) {
      const unsigned line = i;
      const unsigned offset = line * linesize;
      if ((line % 2) == 0) {
        tmp += data[offset] + line;
      } else {
        data[offset] = tmp;
      }
    }
  }
};

static void capacity_init(int argc, char *argv[],
                       const benchmark_config_t *const config) {
  size = config->size * 2;
  linesize = config->line_size;
  num_lines = config->size / config->line_size;

  static struct option longopts[] = {
      {"capacity-rounds", required_argument, NULL, 'r'}, {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --capacity-rounds argument '%s': %s\n",
                optarg, strerror(errno));
      }
      rounds = (unsigned)tmp;
    } break;
    case ':':
    default:;
    }
  }
}

static void *init_argument(void *arg_) {
  assert(arg_ == NULL);

  capacity *arg = new capacity(size);

  return arg;
}

static void destroy_argument(void *arg_) {
  capacity *arg = (capacity *)arg_;
  delete arg;
}

static void *call_work(void *arg_) {
  capacity *arg = (capacity *)arg_;

  for (unsigned round = 0; round < rounds; ++round) {
    arg->work();
  }

  return NULL;
}

benchmark_t capacity_ops = {
    "capacity",       capacity_init, init_argument, NULL,
    destroy_argument, call_work,     NULL,
};

