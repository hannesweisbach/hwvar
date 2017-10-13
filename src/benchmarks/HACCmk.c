#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wconversion"
#pragma clang diagnostic ignored "-Wdouble-promotion"

//
// Argonne Leadership Computing Facility
// Vitali Morozov morozov@anl.gov
//


#include <math.h>

static void Step10_orig(int count1, float xxi, float yyi, float zzi,
                        float fsrrmax2, float mp_rsm2, float *xx1, float *yy1,
                        float *zz1, float *mass1, float *dxi, float *dyi,
                        float *dzi) {

  const float ma0 = 0.269327, ma1 = -0.0750978, ma2 = 0.0114808,
              ma3 = -0.00109313, ma4 = 0.0000605491, ma5 = -0.00000147177;

  float dxc, dyc, dzc, m, r2, f, xi, yi, zi;
  int j;

  xi = 0.;
  yi = 0.;
  zi = 0.;

  for (j = 0; j < count1; j++) {
    dxc = xx1[j] - xxi;
    dyc = yy1[j] - yyi;
    dzc = zz1[j] - zzi;

    r2 = dxc * dxc + dyc * dyc + dzc * dzc;

    m = (r2 < fsrrmax2) ? mass1[j] : 0.0f;

    f = pow(r2 + mp_rsm2, -1.5) -
        (ma0 + r2 * (ma1 + r2 * (ma2 + r2 * (ma3 + r2 * (ma4 + r2 * ma5)))));

    f = (r2 > 0.0f) ? m * f : 0.0f;

    xi = xi + f * dxc;
    yi = yi + f * dyc;
    zi = zi + f * dzc;
  }

  *dxi = xi;
  *dyi = yi;
  *dzi = zi;
}

#pragma clang diagnostic pop

#define NC (32 * 1024) /* Cache size in bytes */
#define N 15000        /* Vector length, must be divisible by 4  15000 */

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "HACCmk.h"


//TODO: clear cache after each run?

typedef struct {
  float xx[N], yy[N], zz[N], mass[N], vx1[N], vy1[N], vz1[N];

  char M1[NC], M2[NC];
  int count;
} HACCmk_args_t;

static void *HACCmk_argument_init(void *arg_) {
  assert(arg_ == NULL);
  HACCmk_args_t *arg = (HACCmk_args_t *)malloc(sizeof(HACCmk_args_t));

  arg->count = 327;

  arg->xx[0] = 0.f;
  arg->yy[0] = 0.f;
  arg->zz[0] = 0.f;
  arg->mass[0] = 2.f;

  return arg;
}

static void HACCmk_argument_destroy(void *arg) { free(arg); }

static void *HACCmk_work(void *arg_) {
#ifdef VALIDATE
  double final = 0.0;
#endif
  const float fcoeff = 0.23f;
  const float fsrrmax2 = 0.5f;
  const float mp_rsm2 = 0.03f;

  HACCmk_args_t *arg = (HACCmk_args_t *)arg_;

  for (int n = 400; n < N; n = n + 20) {
    float dx1 = 1.0f / (float)n;
    float dy1 = 2.0f / (float)n;
    float dz1 = 3.0f / (float)n;

    for (int i = 1; i < n; i++) {
      arg->xx[i] = arg->xx[i - 1] + dx1;
      arg->yy[i] = arg->yy[i - 1] + dy1;
      arg->zz[i] = arg->zz[i - 1] + dz1;
      arg->mass[i] = (float)i * 0.01f + arg->xx[i];
    }

    memset(arg->vx1, 0, (unsigned)n * sizeof(float));
    memset(arg->vy1, 0, (unsigned)n * sizeof(float));
    memset(arg->vz1, 0, (unsigned)n * sizeof(float));

    for (int i = 0; i < arg->count; ++i) {
      Step10_orig(n, arg->xx[i], arg->yy[i], arg->zz[i], fsrrmax2, mp_rsm2,
                  arg->xx, arg->yy, arg->zz, arg->mass, &dx1, &dy1, &dz1);

      arg->vx1[i] = arg->vx1[i] + dx1 * fcoeff;
      arg->vy1[i] = arg->vy1[i] + dy1 * fcoeff;
      arg->vz1[i] = arg->vz1[i] + dz1 * fcoeff;
    }

#ifdef VALIDATE
    for (int i = 0; i < n; i++) {
      final += (double)arg->vx1[i] + (double)arg->vy1[i] + (double)arg->vz1[i];
    }
#endif
  }

#ifdef VALIDATE
  printf("Result validation: %18.8lf\n", final);
  printf("Result expected  : 6636045675.12190628\n");
#endif
  return NULL;
}

benchmark_t HACCmk_ops = {.name = "HACCmk",
                          .init = NULL,
                          .init_arg = HACCmk_argument_init,
                          .reset_arg = NULL,
                          .free_arg = HACCmk_argument_destroy,
                          .call = HACCmk_work,
                          .state = NULL};

