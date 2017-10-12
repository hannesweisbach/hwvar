#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <unistd.h>
#include <sched.h>

#ifdef USE_CBLAS
#include "cblas.h"
#elif defined(USE_NVBLAS)
#include "nvblas.h"
#elif defined(USE_MKL)
#include "mkl.h"
#endif

#include "dgemm.h"

static void do_dgemm(
		double *matrixA,
		double *matrixB,
		double *matrixC,
		int N,
		double alpha,
		double beta,
		int repeats)
{
	int i, j, k, r;
	// ------------------------------------------------------- //
	// VENDOR NOTIFICATION: START MODIFIABLE REGION
	//
	// Vendor is able to change the lines below to call optimized
	// DGEMM or other matrix multiplication routines. Do *NOT*
	// change any lines above this statement.
	// ------------------------------------------------------- //

	double sum = 0;

	// Repeat multiple times
	for(r = 0; r < repeats; r++) {
#if defined( USE_MKL ) || defined (USE_CBLAS)
        cblas_dgemm(CblasRowMajor, CblasNoTrans, CblasNoTrans,
            N, N, N, alpha, matrixA, N, matrixB, N, beta, matrixC, N);
#elif defined( USE_NVBLAS )
		char transA = 'N';
		char transB = 'N';

		dgemm(&transA, &transB, &N, &N, &N, &alpha, matrixA, &N,
			matrixB, &N, &beta, matrixC, &N);
#else
//		#pragma omp parallel for private(sum)
		for(i = 0; i < N; i++) {
			for(j = 0; j < N; j++) {
				sum = 0;

				for(k = 0; k < N; k++) {
					sum += matrixA[i*N + k] * matrixB[k*N + j];
				}

				matrixC[i*N + j] = (alpha * sum) + (beta * matrixC[i*N + j]);
			}
		}
#endif
	}

	// ------------------------------------------------------- //
	// VENDOR NOTIFICATION: END MODIFIABLE REGION
	// ------------------------------------------------------- //
}

typedef struct {
  double *restrict matrixA;
  double *restrict matrixB;
  double *restrict matrixC;
  double alpha;
  double beta;
  int N;
  int repeats;
} dgemm_thread_args_t;

static unsigned N = 36;
static unsigned repeats = 8192;

static void dgemm_init(int argc, char *argv[]) {
  static struct option longopts[] = {
      {"dgemm-N", required_argument, NULL, 'N'},
      {"dgemm-repetions", required_argument, NULL, 'r'},
      {NULL, 0, NULL, 0}};

  while (1) {
    int c = getopt_long(argc, argv, "-N:r:", longopts, NULL);
    if (c == -1)
      break;
    errno = 0;
    switch (c) {
    case 'N': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --dgemm-N argument '%s': %s\n", optarg,
                strerror(errno));
        exit(EXIT_FAILURE);
      }
      N = (unsigned)tmp;
    } break;
    case 'r': {
      unsigned long tmp = strtoul(optarg, NULL, 0);
      if (errno == EINVAL || errno == ERANGE || tmp > INT_MAX) {
        fprintf(stderr, "Could not parse --dgemm-N argument '%s': %s\n", optarg,
                strerror(errno));
      }
      repeats = (unsigned)tmp;
    } break;
    case ':':
    default:;
    }
  }
}

static void *init_argument(void *arg_) {
  assert(arg_ == NULL);
  dgemm_thread_args_t *arg =
      (dgemm_thread_args_t *)malloc(sizeof(dgemm_thread_args_t));

  arg->N = (int)N;
  arg->repeats = (int)repeats;
  arg->alpha = 1.0;
  arg->beta = 1.0;
  arg->matrixA = (double *)malloc(sizeof(double) * N * N);
  arg->matrixB = (double *)malloc(sizeof(double) * N * N);
  arg->matrixC = (double *)malloc(sizeof(double) * N * N);
  for (unsigned j = 0; j < N; j++) {
    for (unsigned k = 0; k < N; k++) {
      arg->matrixA[j * N + k] = 2.0;
      arg->matrixB[j * N + k] = 0.5;
      arg->matrixC[j * N + k] = 1.0;
    }
  }

  return arg;
}

static void destroy_argument(void *arg_) {
  dgemm_thread_args_t *arg = (dgemm_thread_args_t *)arg_;

  free(arg->matrixA);
  free(arg->matrixB);
  free(arg->matrixC);
  free(arg);
}

static void *call_work(void *arg_) {
  dgemm_thread_args_t *arg = (dgemm_thread_args_t *)arg_;
  do_dgemm(arg->matrixA, arg->matrixB, arg->matrixC, arg->N, arg->alpha,
           arg->beta, arg->repeats);
  return NULL;
}

benchmark_t dgemm_ops = {.name = "dgemm",
                         .init = dgemm_init,
                         .init_arg = init_argument,
                         .free_arg = destroy_argument,
                         .call = call_work,
                         .state = NULL};

#if 0
double init_and_do_dgemm(
		char *hostname,
		double *matrixA,
		double *matrixB,
		double *matrixC,
		int N,
		double alpha,
		double beta,
		int repeats)
{
	int i, j, k, r;
	int cpu;

	cpu = sched_getcpu();

	#pragma omp parallel for
	for(i = 0; i < N; i++) {
		for(j = 0; j < N; j++) {
			matrixA[i*N + j] = 2.0;
			matrixB[i*N + j] = 0.5;
			matrixC[i*N + j] = 1.0;
		}
	}

	// Do a warm up round
	do_dgemm(matrixA, matrixB, matrixC, N, alpha, beta, 1);

	printf("Performing multiplication...\n");

	const double start = get_seconds();

	do_dgemm(matrixA, matrixB, matrixC, N, alpha, beta, repeats);

	// ------------------------------------------------------- //
	// DO NOT CHANGE CODE BELOW
	// ------------------------------------------------------- //

	const double end = get_seconds();

	// Account for warm up round
	++repeats;

	printf("Calculating matrix check...\n");

	double final_sum = 0;
	long long int count     = 0;

	#pragma omp parallel for reduction(+:final_sum, count)
	for(i = 0; i < N; i++) {
		for(j = 0; j < N; j++) {
			final_sum += matrixC[i*N + j];
			count++;
		}
	}

	double N_dbl = (double) N;
	double matrix_memory = (3 * N_dbl * N_dbl) * ((double) sizeof(double));

	printf("\n");
	printf("===============================================================\n");

	const double count_dbl = (double) count;
	const double scaled_result = (final_sum / (count_dbl * repeats));

	printf("Final Sum is:         %f\n", scaled_result);

	const double check_sum = N_dbl + (1.0 / (double) (repeats));
	const double allowed_margin = 1.0e-8;

	if( (check_sum >= (scaled_result - allowed_margin)) &&
		(check_sum <= (scaled_result + allowed_margin)) ) {
		printf(" -> Solution check PASSED successfully.\n");
	} else {
		printf(" -> Solution check FAILED.\n");
	}

	printf("Memory for Matrices:  %f MB\n",
		(matrix_memory / (1024 * 1024)));

	const double time_taken = (end - start);

	printf("Multiply time:        %f seconds\n", time_taken);

	const double flops_computed = (N_dbl * N_dbl * N_dbl * 2.0 * (double)(repeats)) +
        	(N_dbl * N_dbl * 2 * (double)(repeats));

	printf("FLOPs computed:       %f\n", flops_computed);
	double gflopsps = (flops_computed / time_taken) / 1000000000.0;
	printf("%s, CPU: %d, GFLOP/s rate:         %f GF/s\n",
			hostname, cpu, gflopsps);

	printf("===============================================================\n");
	printf("\n");

	return gflopsps;
}

// ------------------------------------------------------- //
// Function: main
//
// Modify only in permitted regions (see comments in the
// function)
// ------------------------------------------------------- //
int main(int argc, char* argv[]) {

	// ------------------------------------------------------- //
	// DO NOT CHANGE CODE BELOW
	// ------------------------------------------------------- //

	int N = 256;
	int repeats = 30;

	double alpha = 1.0;
	double beta  = 1.0;

	char hostname[1024];
	char filename[1024];
    cpu_set_t cpuset;
	int cpu, nr_cpus;
	FILE *lf;
	int rank;

#if 0
	MPI_Init(&argc, &argv);
	MPI_Comm_rank(MPI_COMM_WORLD, &rank);
#endif

	if (gethostname(hostname, sizeof(hostname)) < 0) {
		perror("error: obtaining hostname\n");
		exit(-1);
	}

	memset(&cpuset, 0, sizeof(cpuset));
	if ((sched_getaffinity(0, sizeof(cpu_set_t), &cpuset)) < 0) {
		perror("error: sched_getaffinity");
		exit(-1);
	}

	nr_cpus = 0;
	for (cpu = 0; cpu < (8 * sizeof(cpuset)); cpu++) {
		if (!CPU_ISSET(cpu, &cpuset)) {
			continue;
		}

		++nr_cpus;
	}

	sprintf(filename, "%s-rank_%d-nr_cpus_%d.csv", hostname, rank, nr_cpus);
	lf = fopen(filename, "wb+");
	if (!lf) {
		perror("error: fopen");
		exit(-1);
	}

	if(argc > 1) {
		N = atoi(argv[1]);
		printf("Matrix size input by command line: %d\n", N);

		if(argc > 2) {
			repeats = atoi(argv[2]);

			if(repeats < 30) {
				fprintf(stderr, "Error: repeats must be at least 30, setting is: %d\n", repeats);
				exit(-1);
			}

			printf("Repeat multiply %d times.\n", repeats);

            if(argc > 3) {
                alpha = (double) atof(argv[3]);

                if(argc > 4) {
                    beta = (double) atof(argv[4]);
                }
            }
		} else {
			printf("Repeat multiply defaulted to %d\n", repeats);
		}
	} else {
		printf("Matrix size defaulted to %d\n", N);
	}

    printf("Alpha =    %f\n", alpha);
    printf("Beta  =    %f\n", beta);

	/*
	if(N < 128) {
		printf("Error: N (%d) is less than 128, the matrix is too small.\n", N);
		exit(-1);
	}
	*/

	printf("Allocating Matrices...\n");

	double* DGEMM_RESTRICT matrixA = (double*) malloc(sizeof(double) * N * N);
	double* DGEMM_RESTRICT matrixB = (double*) malloc(sizeof(double) * N * N);
	double* DGEMM_RESTRICT matrixC = (double*) malloc(sizeof(double) * N * N);

	//printf("Allocation complete, populating with values...\n");

	for (cpu = 0; cpu < (8 * sizeof(cpuset)); cpu++) {
		cpu_set_t target_cpu;
		double gflopsps;
		int iter;

		if (!CPU_ISSET(cpu, &cpuset)) {
			continue;
		}

		memset(&target_cpu, 0, sizeof(cpu_set_t));
		CPU_SET(cpu, &target_cpu);

		if (sched_setaffinity(0, sizeof(cpu_set_t), &target_cpu) < 0) {
			perror("error: sched_setaffinity");
			exit(-1);
		}
		fprintf(lf, "%d", cpu);

		for (iter = 0; iter < 5; ++iter) {
			gflopsps = init_and_do_dgemm(hostname,
					matrixA, matrixB, matrixC, N, alpha, beta, repeats);
		}

		for (iter = 0; iter < 10; ++iter) {
			gflopsps = init_and_do_dgemm(hostname,
					matrixA, matrixB, matrixC, N, alpha, beta, repeats);
			fprintf(lf, ",%g", gflopsps);
		}

		fprintf(lf, "\n");
		fflush(lf);
		fflush(stdout);
	}

	fclose(lf);
	free(matrixA);
	free(matrixB);
	free(matrixC);

#if 0
	MPI_Barrier(MPI_COMM_WORLD);
	MPI_Finalize();
#endif

	return 0;
}
#endif
