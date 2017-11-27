#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>

#include <getopt.h>

#include "hpccg.h"
#include "HPCCG/HPCCG.hpp"
#include "HPCCG/generate_matrix.hpp"

struct hpccg_args {
  HPC_Sparse_Matrix *A;
  double *x, *b, *xexact;
  double *r;
  double *p;
  double *Ap;
  int nx, ny, nz;
};

static int rounds = 10;

static void hpccg_init(int argc, char *argv[]) {
  static struct option longopts[] = {
      {"hpccg-rounds", required_argument, NULL, 'r'},
      {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --hpccg-rounds argument '%s': %s\n", optarg,
                strerror(errno));
      }
      rounds = static_cast<int>(tmp);
    } break;
    case ':':
    default:;
    }
  }
}

static void *init_argument(void *) {
  struct hpccg_args *args = new hpccg_args;

  args->nx = 5;
  args->ny = 4;
  args->nz = 3;

  allocate(args->nx, args->ny, args->nz, &args->A, &args->x, &args->b,
           &args->xexact, &args->r, &args->p, &args->Ap);

  return args;
}

static void *hpccg_work(void *arg_) {
  struct hpccg_args *args = static_cast<struct hpccg_args *>(arg_);
  int niters = 0;
  double normr = 0.0;
  int max_iter = 150;
  double tolerance =
      0.0; // Set tolerance to zero to make all runs do max_iter iterations
  for (int round = 0; round < rounds; ++round) {
    generate_matrix(args->nx, args->ny, args->nz, &args->A, &args->x, &args->b,
                    &args->xexact);
    HPCCG(args->A, args->b, args->x, max_iter, tolerance, niters, normr, args->r, args->p, args->Ap);
  }
  return NULL;
}

benchmark_t hpccg_ops = {
    .name = "hpccg",
    .init = hpccg_init,
    .init_arg = init_argument,
    .reset_arg = NULL,
    .free_arg = NULL,
    .call = hpccg_work,
    .state = NULL,
    .params = {.data_size = sizeof(double), .datasets = 63, .power = 3}};
