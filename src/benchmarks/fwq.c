#include <errno.h>
#include <limits.h>
#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "fwq.h"

static int rounds = 10;
static void fwq_init(int argc, char *argv[],
                     const benchmark_config_t *const config) {
  static struct option longopts[] = {
      {"fwq-rounds", required_argument, NULL, 'r'}, {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --dgemm-rounds argument '%s': %s\n",
                optarg, strerror(errno));
      }
      if (tmp <= 0) {
        fprintf(stderr, "--fwq-rounds argument has to be positive.\n" );
      }
      rounds = (int)tmp;
    } break;
    case ':':
    default:;
    }
  }

}

typedef struct {
  int64_t cnt;
} fwq_arg_t;

static void *fwq_arg_init(void *arg_) {
  fwq_arg_t *arg = (fwq_arg_t *)malloc(sizeof(fwq_arg_t));
  return arg;
}

static void fwq_arg_free(void *arg_) {
  fwq_arg_t *arg = (fwq_arg_t *)arg_;
  free(arg);
}

static void *call_work(void *arg_) {
  const int64_t wl = 1LL << 20;
  int64_t count = -wl;
  for (int round = 0; round < rounds; ++round) {
    for (count = -wl; count < 0;) {
      count++;
      __asm__ __volatile__("");
    }
  }

  {
    /* pretend to use the result */
    fwq_arg_t *arg = (fwq_arg_t *)arg_;
    arg->cnt = count;
  }

  return NULL;
}

benchmark_t fwq_ops = {
    .name = "fwq",
    .init = fwq_init,
    .init_arg = fwq_arg_init,
    .reset_arg = NULL,
    .free_arg = fwq_arg_free,
    .call = call_work,
    .state = NULL,
};

