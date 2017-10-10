/*-----------------------------------------------------------------------*/
/* Program: STREAM                                                       */
/* Revision: $Id: stream.c,v 5.10 2013/01/17 16:01:06 mccalpin Exp mccalpin $ */
/* Original code developed by John D. McCalpin                           */
/* Programmers: John D. McCalpin                                         */
/*              Joe R. Zagar                                             */
/*                                                                       */
/* This program measures memory transfer rates in MB/s for simple        */
/* computational kernels coded in C.                                     */
/*-----------------------------------------------------------------------*/
/* Copyright 1991-2013: John D. McCalpin                                 */
/*-----------------------------------------------------------------------*/
/* License:                                                              */
/*  1. You are free to use this program and/or to redistribute           */
/*     this program.                                                     */
/*  2. You are free to modify this program for your own use,             */
/*     including commercial use, subject to the publication              */
/*     restrictions in item 3.                                           */
/*  3. You are free to publish results obtained from running this        */
/*     program, or from works that you derive from this program,         */
/*     with the following limitations:                                   */
/*     3a. In order to be referred to as "STREAM benchmark results",     */
/*         published results must be in conformance to the STREAM        */
/*         Run Rules, (briefly reviewed below) published at              */
/*         http://www.cs.virginia.edu/stream/ref.html                    */
/*         and incorporated herein by reference.                         */
/*         As the copyright holder, John McCalpin retains the            */
/*         right to determine conformity with the Run Rules.             */
/*     3b. Results based on modified source code or on runs not in       */
/*         accordance with the STREAM Run Rules must be clearly          */
/*         labelled whenever they are published.  Examples of            */
/*         proper labelling include:                                     */
/*           "tuned STREAM benchmark results"                            */
/*           "based on a variant of the STREAM benchmark code"           */
/*         Other comparable, clear, and reasonable labelling is          */
/*         acceptable.                                                   */
/*     3c. Submission of results to the STREAM benchmark web site        */
/*         is encouraged, but not required.                              */
/*  4. Use of this program or creation of derived works based on this    */
/*     program constitutes acceptance of these licensing restrictions.   */
/*  5. Absolutely no warranty is expressed or implied.                   */
/*-----------------------------------------------------------------------*/

/*-----------------------------------------------------------------------
 * INSTRUCTIONS:
 *
 *	1) STREAM requires different amounts of memory to run on different
 *           systems, depending on both the system cache size(s) and the
 *           granularity of the system timer.
 *     You should adjust the value of 'STREAM_ARRAY_SIZE' (below)
 *           to meet *both* of the following criteria:
 *       (a) Each array must be at least 4 times the size of the
 *           available cache memory. I don't worry about the difference
 *           between 10^6 and 2^20, so in practice the minimum array size
 *           is about 3.8 times the cache size.
 *           Example 1: One Xeon E3 with 8 MB L3 cache
 *               STREAM_ARRAY_SIZE should be >= 4 million, giving
 *               an array size of 30.5 MB and a total memory requirement
 *               of 91.5 MB.  
 *           Example 2: Two Xeon E5's with 20 MB L3 cache each (using OpenMP)
 *               STREAM_ARRAY_SIZE should be >= 20 million, giving
 *               an array size of 153 MB and a total memory requirement
 *               of 458 MB.  
 *       (b) The size should be large enough so that the 'timing calibration'
 *           output by the program is at least 20 clock-ticks.  
 *           Example: most versions of Windows have a 10 millisecond timer
 *               granularity.  20 "ticks" at 10 ms/tic is 200 milliseconds.
 *               If the chip is capable of 10 GB/s, it moves 2 GB in 200 msec.
 *               This means the each array must be at least 1 GB, or 128M elements.
 *
 *      Version 5.10 increases the default array size from 2 million
 *          elements to 10 million elements in response to the increasing
 *          size of L3 caches.  The new default size is large enough for caches
 *          up to 20 MB. 
 *      Version 5.10 changes the loop index variables from "register int"
 *          to "ssize_t", which allows array indices >2^32 (4 billion)
 *          on properly configured 64-bit systems.  Additional compiler options
 *          (such as "-mcmodel=medium") may be required for large memory runs.
 *
 *      Array size can be set at compile time without modifying the source
 *          code for the (many) compilers that support preprocessor definitions
 *          on the compile line.  E.g.,
 *                gcc -O -DSTREAM_ARRAY_SIZE=100000000 stream.c -o stream.100M
 *          will override the default size of 10M with a new size of 100M elements
 *          per array.
 */

/*  2) STREAM runs each kernel "NTIMES" times and reports the *best* result
 *         for any iteration after the first, therefore the minimum value
 *         for NTIMES is 2.
 *      There are no rules on maximum allowable values for NTIMES, but
 *         values larger than the default are unlikely to noticeably
 *         increase the reported performance.
 *      NTIMES can also be set on the compile line without changing the source
 *         code using, for example, "-DNTIMES=7".
 */

/*  Users are allowed to modify the "OFFSET" variable, which *may* change the
 *         relative alignment of the arrays (though compilers may change the 
 *         effective offset by making the arrays non-contiguous on some systems). 
 *      Use of non-zero values for OFFSET can be especially helpful if the
 *         STREAM_ARRAY_SIZE is set to a value close to a large power of 2.
 *      OFFSET can also be set on the compile line without changing the source
 *         code using, for example, "-DOFFSET=56".
 */
#ifndef OFFSET
#   define OFFSET	0
#endif

/*
 *	3) Compile the code with optimization.  Many compilers generate
 *       unreasonably bad code before the optimizer tightens things up.  
 *     If the results are unreasonably good, on the other hand, the
 *       optimizer might be too smart for me!
 *
 *     For a simple single-core version, try compiling with:
 *            cc -O stream.c -o stream
 *     This is known to work on many, many systems....
 *
 *     To use multiple cores, you need to tell the compiler to obey the OpenMP
 *       directives in the code.  This varies by compiler, but a common example is
 *            gcc -O -fopenmp stream.c -o stream_omp
 *       The environment variable OMP_NUM_THREADS allows runtime control of the 
 *         number of threads/cores used when the resulting "stream_omp" program
 *         is executed.
 *
 *     To run with single-precision variables and arithmetic, simply add
 *         -DSTREAM_TYPE=float
 *     to the compile line.
 *     Note that this changes the minimum array sizes required --- see (1) above.
 *
 *     The preprocessor directive "TUNED" does not do much -- it simply causes the 
 *       code to call separate functions to execute each kernel.  Trivial versions
 *       of these functions are provided, but they are *not* tuned -- they just 
 *       provide predefined interfaces to be replaced with tuned code.
 *
 *
 *	4) Optional: Mail the results to mccalpin@cs.virginia.edu
 *	   Be sure to include info that will help me understand:
 *		a) the computer hardware configuration (e.g., processor model, memory type)
 *		b) the compiler name/version and compilation flags
 *      c) any run-time information (such as OMP_NUM_THREADS)
 *		d) all of the output from the test case.
 *
 * Thanks!
 *
 *-----------------------------------------------------------------------*/

#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stream.h"

#ifndef STREAM_TYPE
#define STREAM_TYPE double
#endif

typedef struct {
  STREAM_TYPE *a;
  STREAM_TYPE *b;
  STREAM_TYPE *c;
} STREAM_t;

static int ntimes = 10;
static ssize_t STREAM_ARRAY_SIZE = 10000000;

static void STREAM_Init(int argc, char *argv[]) {
  static struct option longopts[] = {
      {"STREAM-n", required_argument, NULL, 'n'},
      {"STREAM-size", required_argument, NULL, 's'},
      {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-n:s:", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'n': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --STREAM-n argument '%s': %s\n", optarg,
                strerror(errno));
        exit(EXIT_FAILURE);
      }
      ntimes = (int)tmp;
    } break;
    case 's': {
       long long tmp = strtoll(optarg, NULL, 0);
       if (errno == EINVAL || errno == ERANGE || tmp < 0) {
         fprintf(stderr, "Could not parse --STREAM-size argument '%s': %s\n",
                 optarg, strerror(errno));
      }
      STREAM_ARRAY_SIZE = tmp;
    } break;
    case ':':
    default:;
    }
  }
}

static void *STREAM_argument_init(void *arg_) {
  assert(arg_ == NULL);
  STREAM_t *arg = (STREAM_t *)malloc(sizeof(STREAM_t));
  if (arg == NULL) {
    exit(EXIT_FAILURE);
  }

  size_t size = (unsigned long long)STREAM_ARRAY_SIZE + OFFSET;
  arg->a = (STREAM_TYPE *)malloc(sizeof(STREAM_TYPE) * size);
  arg->b = (STREAM_TYPE *)malloc(sizeof(STREAM_TYPE) * size);
  arg->c = (STREAM_TYPE *)malloc(sizeof(STREAM_TYPE) * size);

  if (arg->a == NULL || arg->b == NULL || arg->c == NULL) {
    exit(EXIT_FAILURE);
  }

  for (int j = 0; j < STREAM_ARRAY_SIZE; j++) {
    arg->a[j] = 1.0;
    arg->b[j] = 2.0;
    arg->c[j] = 0.0;
  }

  return arg;
}

static void STREAM_argument_destroy(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;

  free(arg->a);
  free(arg->b);
  free(arg->c);
  free(arg);
}

static void *STREAM_Copy_call(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;

  for (int k = 0; k < ntimes; ++k) {
    for (ssize_t j = 0; j < STREAM_ARRAY_SIZE; j++)
      arg->c[j] = arg->a[j];
  }

  return NULL;
}

static void *STREAM_Scale_call(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;
  const STREAM_TYPE scalar = (STREAM_TYPE)3.0;

  for (int k = 0; k < ntimes; ++k) {
    for (ssize_t j = 0; j < STREAM_ARRAY_SIZE; j++)
      arg->b[j] = scalar * arg->c[j];
  }

  return NULL;
}

static void *STREAM_Add_call(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;

  for (int k = 0; k < ntimes; ++k) {
    for (ssize_t j = 0; j < STREAM_ARRAY_SIZE; j++)
      arg->c[j] = arg->a[j] + arg->b[j];
  }

  return NULL;
}

static void *STREAM_Triad_call(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;
  const STREAM_TYPE scalar = (STREAM_TYPE)3.0;

  for (int k = 0; k < ntimes; ++k) {
    for (ssize_t j = 0; j < STREAM_ARRAY_SIZE; j++)
      arg->a[j] = arg->b[j] + scalar * arg->c[j];
  }

  return NULL;
}

static void *STREAM_call(void *arg_) {
  STREAM_t *arg = (STREAM_t *)arg_;
  const int ntimes_copy = ntimes;
  ntimes = 1;

  for (int k = 0; k < ntimes_copy; ++k) {
    STREAM_Copy_call(arg);
    STREAM_Scale_call(arg);
    STREAM_Add_call(arg);
    STREAM_Triad_call(arg);
  }

  ntimes = ntimes_copy;

  return NULL;
}

benchmark_t STREAM_Copy = {"STREAM_Copy",
                           STREAM_Init,
                           STREAM_argument_init,
                           STREAM_argument_destroy,
                           NULL,
                           STREAM_Copy_call,
                           NULL};

benchmark_t STREAM_Scale = {"STREAM_Scale",
                            STREAM_Init,
                            STREAM_argument_init,
                            STREAM_argument_destroy,
                            NULL,
                            STREAM_Scale_call,
                            NULL};

benchmark_t STREAM_Add = {"STREAM_Add",
                          STREAM_Init,
                          STREAM_argument_init,
                          STREAM_argument_destroy,
                          NULL,
                          STREAM_Add_call,
                          NULL};

benchmark_t STREAM_Triad = {"STREAM_Triad",
                            STREAM_Init,
                            STREAM_argument_init,
                            STREAM_argument_destroy,
                            NULL,
                            STREAM_Triad_call,
                            NULL};

benchmark_t STREAM = {"STREAM",
                      STREAM_Init,
                      STREAM_argument_init,
                      STREAM_argument_destroy,
                      NULL,
                      STREAM_call,
                      NULL};

