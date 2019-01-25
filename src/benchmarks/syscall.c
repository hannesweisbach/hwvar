#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "syscall.h"

static int rounds = 10;
static void syscall_init(int argc, char *argv[],
                     const benchmark_config_t *const config) {
  static struct option longopts[] = {
      {"syscall-rounds", required_argument, NULL, 'r'}, {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --syscall-rounds argument '%s': %s\n",
                optarg, strerror(errno));
      }
      if (tmp <= 0) {
        fprintf(stderr, "--syscall-rounds argument has to be positive.\n" );
      }
      rounds = (int)tmp;
    } break;
    case ':':
    default:;
    }
  }

}

static void *syscall_arg_init(void *arg_) { return NULL; }
static void syscall_arg_free(void *arg_) {}

static void *call_work(void *arg_) {
  for (int round = 0; round < rounds; ++round) {
    syscall(732);
  }

  return NULL;
}

benchmark_t syscall_ops = {
    .name = "syscall",
    .init = syscall_init,
    .init_arg = syscall_arg_init,
    .reset_arg = NULL,
    .free_arg = syscall_arg_free,
    .call = call_work,
    .state = NULL,
};


