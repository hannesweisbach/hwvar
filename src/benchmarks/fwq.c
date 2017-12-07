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

static void *call_work(void *arg_) {
  for (int round = 0; round < rounds; ++round) {

    static int64_t wl = 1 << 20;
    for (int64_t count = -wl; count < 0;) {
      count++;
      __asm__ __volatile__("");
    }
  }
  return NULL;
}

benchmark_t fwq_ops = {
    .name = "fwq",
    .init = fwq_init,
    .init_arg = NULL,
    .reset_arg = NULL,
    .free_arg = NULL,
    .call = call_work,
    .state = NULL,
};

