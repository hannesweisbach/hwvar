#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wdouble-promotion"

//
// Argonne Leadership Computing Facility
// Vitali Morozov morozov@anl.gov
//

//#define VALIDATE

#include <math.h>

typedef struct p_3f {
  float x;
  float y;
  float z;
} p3f_t;

static p3f_t Step10_orig(const unsigned count1, const float xxi,
                         const float yyi, const float zzi, const float fsrrmax2,
                         const float mp_rsm2, const float *const restrict xx1,
                         const float *const restrict yy1,
                         const float *const restrict zz1,
                         const float *const restrict mass1) {

  const float ma0 = 0.269327, ma1 = -0.0750978, ma2 = 0.0114808,
              ma3 = -0.00109313, ma4 = 0.0000605491, ma5 = -0.00000147177;

  p3f_t i = {0.0, 0.0, 0.0};

  for (unsigned j = 0; j < count1; j++) {
    const float dxc = xx1[j] - xxi;
    const float dyc = yy1[j] - yyi;
    const float dzc = zz1[j] - zzi;

    const float r2 = dxc * dxc + dyc * dyc + dzc * dzc;

    const float m = (r2 < fsrrmax2) ? mass1[j] : 0.0f;

    const float f_ =
        pow(r2 + mp_rsm2, -1.5) -
        (ma0 + r2 * (ma1 + r2 * (ma2 + r2 * (ma3 + r2 * (ma4 + r2 * ma5)))));

    const float f = (r2 > 0.0f) ? m * f_ : 0.0f;

    i.x = i.x + f * dxc;
    i.y = i.y + f * dyc;
    i.z = i.z + f * dzc;
  }

  return i;
}

#pragma clang diagnostic pop

//#define NC (32 * 1024) /* Cache size in bytes */

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HACCmk.h"

// TODO: clear cache after each run?
static unsigned int N = 15000; /* Vector length, must be divisible by 4 */
static int iterations = 1;

typedef struct {
  float *xx;
  float *yy;
  float *zz;
  float *mass;
  float *vx1;
  float *vy1;
  float *vz1;

  //char M1[NC], M2[NC];
} HACCmk_args_t;

static void HACCmk_init(int argc, char *argv[]) {
  static struct option longopts[] = {
      {"HACCmk-size", required_argument, NULL, 's'},
      {"HACCmk-rounds", required_argument, NULL, 'i'},
      {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-N:r:", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 's': {
      long tmp = strtol(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --HACCmk-size argument '%s': %s\n",
                optarg, strerror(errno));
        exit(EXIT_FAILURE);
      }

      if (tmp < 400) {
        fprintf(stderr, "Vector size must be at least 400\n");
        exit(EXIT_FAILURE);
      }

      if ((tmp % 4) != 0) {
        fprintf(stderr, "Vector size must be divisible by 4\n");
        exit(EXIT_FAILURE);
      }

      N = (unsigned)tmp;
    } break;
    case 'i': {
      long tmp = strtol(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --dgemm-N argument '%s': %s\n", optarg,
                strerror(errno));
      }
      iterations = (int)tmp;
    } break;
    case ':':
    default:;
    }
  }
}

static void *HACCmk_argument_init(void *arg_) {
  assert(arg_ == NULL);
  HACCmk_args_t *arg = (HACCmk_args_t *)malloc(sizeof(HACCmk_args_t));

  const size_t size = N * sizeof(float);
  arg->xx = (float *)malloc(size);
  arg->yy = (float *)malloc(size);
  arg->zz = (float *)malloc(size);
  arg->mass = (float *)malloc(size);
  arg->vx1 = (float *)malloc(size);
  arg->vy1 = (float *)malloc(size);
  arg->vz1 = (float *)malloc(size);

  if (arg->xx == NULL || arg->yy == NULL || arg->zz == NULL ||
      arg->mass == NULL || arg->vx1 == NULL || arg->vy1 == NULL ||
      arg->vz1 == NULL) {
    fprintf(stderr, "Error allocating memory\n");
    exit(EXIT_FAILURE);
  }

  arg->xx[0] = 0.f;
  arg->yy[0] = 0.f;
  arg->zz[0] = 0.f;
  arg->mass[0] = 2.f;

  return arg;
}

static void HACCmk_argument_destroy(void *arg_) {
  HACCmk_args_t *arg = (HACCmk_args_t *)arg_;
  free(arg->xx);
  free(arg->yy);
  free(arg->zz);
  free(arg->mass);
  free(arg->vx1);
  free(arg->vy1);
  free(arg->vz1);

  free(arg);
}

static void *HACCmk_work(void *arg_) {
#ifdef VALIDATE
  double final = 0.0;
#endif
  const float fcoeff = 0.23f;
  const float fsrrmax2 = 0.5f;
  const float mp_rsm2 = 0.03f;
  const int count = 327; // maybe this can also be used to adjust runtime ?

  HACCmk_args_t *arg = (HACCmk_args_t *)arg_;

  for (int it = 0; it < iterations; ++it) {
    for (unsigned n = 400; n < N; n = n + 20) {
      const float dx1 = 1.0f / (float)n;
      const float dy1 = 2.0f / (float)n;
      const float dz1 = 3.0f / (float)n;

      for (unsigned i = 1; i < n; i++) {
        arg->xx[i] = arg->xx[i - 1] + dx1;
        arg->yy[i] = arg->yy[i - 1] + dy1;
        arg->zz[i] = arg->zz[i - 1] + dz1;
        arg->mass[i] = (float)i * 0.01f + arg->xx[i];
      }

      memset(arg->vx1, 0, n * sizeof(float));
      memset(arg->vy1, 0, n * sizeof(float));
      memset(arg->vz1, 0, n * sizeof(float));

      for (unsigned i = 0; i < count; ++i) {
        p3f_t d = Step10_orig(n, arg->xx[i], arg->yy[i], arg->zz[i], fsrrmax2,
                              mp_rsm2, arg->xx, arg->yy, arg->zz, arg->mass);

        arg->vx1[i] = arg->vx1[i] + d.x * fcoeff;
        arg->vy1[i] = arg->vy1[i] + d.y * fcoeff;
        arg->vz1[i] = arg->vz1[i] + d.z * fcoeff;
      }

#ifdef VALIDATE
      for (unsigned i = 0; i < n; i++) {
        final +=
            (double)arg->vx1[i] + (double)arg->vy1[i] + (double)arg->vz1[i];
      }
#endif
    }
  }

#ifdef VALIDATE
  printf("Result validation: %18.8lf\n", final);
  printf("Result expected  : 6636045675.12190628\n");
#endif
  return NULL;
}

benchmark_t HACCmk_ops = {
    .name = "HACCmk",
    .init = HACCmk_init,
    .init_arg = HACCmk_argument_init,
    .reset_arg = NULL,
    .free_arg = HACCmk_argument_destroy,
    .call = HACCmk_work,
    .state = NULL,
    .params = {.data_size = sizeof(float), .datasets = 7, .power = 1}};

