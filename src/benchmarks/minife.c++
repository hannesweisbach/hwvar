#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstring>
#include <iostream>
#include <vector>

#include <getopt.h>

#include <Box.hpp>
#include <BoxPartition.hpp>
#include <driver.hpp>
#include <Parameters.hpp>

#include "benchmark.h"

namespace miniFE {
// Declare matrix object:

typedef MINIFE_SCALAR Scalar;
typedef MINIFE_LOCAL_ORDINAL LocalOrdinal;
typedef MINIFE_GLOBAL_ORDINAL GlobalOrdinal;

#if defined(MINIFE_ELL_MATRIX)
typedef ELLMatrix<Scalar, LocalOrdinal, GlobalOrdinal> MatrixType;
#else
typedef CSRMatrix<Scalar, LocalOrdinal, GlobalOrdinal> MatrixType;
#endif

template <typename OperatorType, typename VectorType, typename Matvec>
void cg_solve(
    OperatorType &A, const VectorType &b, VectorType &x, Matvec matvec,
    typename OperatorType::LocalOrdinalType max_iter,
    const typename TypeTraits<typename OperatorType::ScalarType>::magnitude_type
        &tolerance,
    typename OperatorType::LocalOrdinalType &num_iters,
    typename TypeTraits<typename OperatorType::ScalarType>::magnitude_type
        &normr) {
  typedef typename OperatorType::ScalarType ScalarType;
  typedef typename OperatorType::GlobalOrdinalType GlobalOrdinalType;
  typedef typename OperatorType::LocalOrdinalType LocalOrdinalType;
  typedef typename TypeTraits<ScalarType>::magnitude_type magnitude_type;

  int myproc = 0;

  if (!A.has_local_indices) {
    std::cerr << "miniFE::cg_solve ERROR, A.has_local_indices is false, needs to be true. This probably means "
       << "miniFE::make_local_matrix(A) was not called prior to calling miniFE::cg_solve."
       << std::endl;
    return;
  }

  size_t nrows = A.rows.size();
  LocalOrdinalType ncols = A.num_cols;

  VectorType r(b.startIndex, nrows);
  VectorType p(0, ncols);
  VectorType Ap(b.startIndex, nrows);

  normr = 0;
  magnitude_type rtrans = 0;
  magnitude_type oldrtrans = 0;

  ScalarType one = 1.0;
  ScalarType zero = 0.0;

  waxpby(one, x, zero, x, p);

  matvec(A, p, Ap);

  waxpby(one, b, -one, Ap, r);

  rtrans = dot(r, r);

  normr = std::sqrt(rtrans);

  magnitude_type brkdown_tol = std::numeric_limits<magnitude_type>::epsilon();

#ifdef MINIFE_DEBUG
  std::ostream& os = outstream();
  os << "brkdown_tol = " << brkdown_tol << std::endl;
#endif


  for(LocalOrdinalType k=1; k <= max_iter && normr > tolerance; ++k) {
    if (k == 1) {
      waxpby(one, r, zero, r, p);
    }
    else {
      oldrtrans = rtrans;
      rtrans = dot(r, r);
      magnitude_type beta = rtrans/oldrtrans;
      waxpby(one, r, beta, p, p);
    }

    normr = std::sqrt(rtrans);

    magnitude_type alpha = 0;
    magnitude_type p_ap_dot = 0;

#ifdef MINIFE_FUSED
    p_ap_dot = matvec_and_dot(A, p, Ap);
#else
    matvec(A, p, Ap);

    p_ap_dot = dot(Ap, p);
#endif

#ifdef MINIFE_DEBUG
    os << "iter " << k << ", p_ap_dot = " << p_ap_dot;
    os.flush();
#endif
    if (p_ap_dot < brkdown_tol) {
      if (p_ap_dot < 0 || breakdown(p_ap_dot, Ap, p)) {
        std::cerr << "miniFE::cg_solve ERROR, numerical breakdown!"<<std::endl;
#ifdef MINIFE_DEBUG
        os << "ERROR, numerical breakdown!"<<std::endl;
#endif
        return;
      }
      else brkdown_tol = 0.1 * p_ap_dot;
    }
    alpha = rtrans/p_ap_dot;
#ifdef MINIFE_DEBUG
    os << ", rtrans = " << rtrans << ", alpha = " << alpha << std::endl;
#endif

#ifdef MINIFE_FUSED
    fused_waxpby(one, x, alpha, p, x, one, r, -alpha, Ap, r);
#else
    waxpby(one, x, alpha, p, x);
    waxpby(one, r, -alpha, Ap, r);
#endif

    num_iters = k;
  }
}

template <typename Scalar, typename LocalOrdinal, typename GlobalOrdinal>
int driver(const Box &global_box, Box &my_box, const Parameters &params,
           const simple_mesh_description<GlobalOrdinal> &mesh, MatrixType &A,
           Vector<Scalar, LocalOrdinal, GlobalOrdinal> &b,
           Vector<Scalar, LocalOrdinal, GlobalOrdinal> &x) {
  int global_nx = global_box[0][1];
  int global_ny = global_box[1][1];
  int global_nz = global_box[2][1];

  int numprocs = 1, myproc = 0;

  /* (re-)set arguments */
  generate_matrix_structure(mesh, A);
  std::fill(b.coefs.begin(), b.coefs.end(), 0);
  std::fill(x.coefs.begin(), x.coefs.end(), 0);

  //Assemble finite-element sub-matrices and sub-vectors into the global
  //linear system:

  assemble_FE_data(mesh, A, b, params);

  //Now apply dirichlet boundary-conditions
  //(Apply the 0-valued surfaces first, then the 1-valued surface last.)

  impose_dirichlet(0.0, A, b, global_nx + 1, global_ny + 1, global_nz + 1,
                   mesh.bc_rows_0);
  impose_dirichlet(1.0, A, b, global_nx + 1, global_ny + 1, global_nz + 1,
                   mesh.bc_rows_1);

  //Transform global indices to local, set up communication information:

  make_local_matrix(A);

  //size_t global_nnz = compute_matrix_stats(A, myproc, numprocs, ydoc);

  //Prepare to perform conjugate gradient solve:

  LocalOrdinal max_iters = 200;
  LocalOrdinal num_iters = 0;
  typedef typename TypeTraits<Scalar>::magnitude_type magnitude;
  magnitude rnorm = 0;
  magnitude tol = std::numeric_limits<magnitude>::epsilon();

  typedef Vector<Scalar,LocalOrdinal,GlobalOrdinal> VectorType;

  bool matvec_with_comm_overlap = params.mv_overlap_comm_comp==1;

  int verify_result = 0;

  if (matvec_with_comm_overlap) {
#ifdef MINIFE_CSR_MATRIX
    rearrange_matrix_local_external(A);
    cg_solve(A, b, x, matvec_overlap<MatrixType,VectorType>(), max_iters, tol,
           num_iters, rnorm);
#else
    std::cout << "ERROR, matvec with overlapping comm/comp only works with CSR matrix."<<std::endl;
#endif
  }
  else {
    cg_solve(A, b, x, matvec_std<MatrixType,VectorType>(), max_iters, tol,
           num_iters, rnorm);
#ifdef MINIFE_DEBUG
    if (myproc == 0) {
      std::cout << "Final Resid Norm: " << rnorm << std::endl;
    }
#endif

    if (params.verify_solution > 0) {
      double tolerance = 0.06;
      bool verify_whole_domain = true;
  #ifdef MINIFE_DEBUG
      verify_whole_domain = true;
  #endif
      if (myproc == 0) {
        if (verify_whole_domain) std::cout << "verifying solution..." << std::endl;
        else std::cout << "verifying solution at ~ (0.5, 0.5, 0.5) ..." << std::endl;
      }
      verify_result = verify_solution(mesh, x, tolerance, verify_whole_domain);
    }
  }

  return verify_result;
}

}//namespace miniFE

using namespace miniFE;
static miniFE::MatrixType make_matrix(
    const simple_mesh_description<miniFE::MatrixType::GlobalOrdinalType>
        &mesh) {
  miniFE::MatrixType A;
  generate_matrix_structure(mesh, A);
  return A;
}

static GlobalOrdinal first_row(const MatrixType &A) {
  return (A.rows.size() > 0) ? A.rows[0] : -1;
}

static Box &local_box(const int numprocs, const int myproc,
                      const Box &global_box, std::vector<Box> &local_boxes) {
  box_partition(0, numprocs, 2, global_box, &local_boxes[0]);

  return local_boxes[myproc];
}

struct minife_args {
  Box global_box_;
  std::vector<Box> local_boxes;
  Box &local_box_;
  simple_mesh_description<GlobalOrdinal> mesh;
  MatrixType A;
  Vector<Scalar, LocalOrdinal, GlobalOrdinal> b;
  Vector<Scalar, LocalOrdinal, GlobalOrdinal> x;

  minife_args(const miniFE::Parameters &params, Box &global_box,
              const int nprocs, const int myproc)
      : global_box_(global_box), local_boxes(nprocs),
        local_box_(local_box(nprocs, myproc, global_box, local_boxes)),
        mesh(global_box_, local_box_), A(make_matrix(mesh)),
        b(first_row(A), A.rows.size()), x(first_row(A), A.rows.size()) {
#if 0
    GlobalOrdinal nrows = A.rows.size();
    double nnz = A.num_nonzeros();

    const unsigned num_bytes =
        sizeof(GlobalOrdinal) * nrows  //  for A.rows
        + sizeof(LocalOrdinal) * nrows // for A.rows_offsets
        + sizeof(GlobalOrdinal) * nnz  // for A.packed_cols
        + sizeof(Scalar) * nnz;        // for A.packed_coefs

    const unsigned vec_size = sizeof(Scalar) * nrows * 5; // for b, x, Ap, r, p
    const unsigned tmp_size = sizeof(GlobalOrdinal) * nrows +
                              sizeof(LocalOrdinal) * nrows +
                              sizeof(int) * nrows * 3;

    std::cout << num_bytes << " bytes Matrix size" << std::endl;
    std::cout << vec_size << " bytes for Vectors" << std::endl;
    std::cout << tmp_size << " bytes for temporaries" << std::endl;
    std::cout << "Sum: " << num_bytes + vec_size + tmp_size << std::endl;
#endif
  }
};

static miniFE::Parameters params;
static int rounds = 10;

static void minife_init(int argc, char *argv[]) {
  static struct option longopts[] = {
      {"minife-rounds", required_argument, NULL, 'r'},
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

  miniFE::get_parameters(argc, argv, params);
  //params.verify_solution = 1;
  params.nx = 4;
  params.ny = 4;
  params.nz = 3;
}

static void *init_argument(void *) {
  Box global_box = {0, params.nx, 0, params.ny, 0, params.nz};

  minife_args *args = new minife_args(params, global_box, 1, 0);

  return args;
}

static void *minife(void *arg_) {

  minife_args *args = static_cast<minife_args *>(arg_);

  for (int round = 0; round < rounds; ++round) {
    int return_code = miniFE::driver<MINIFE_SCALAR, MINIFE_LOCAL_ORDINAL,
                                     MINIFE_GLOBAL_ORDINAL>(
        args->global_box_, args->local_box_, params, args->mesh, args->A,
        args->b, args->x);
  }

  return NULL;
}

benchmark_t minife_ops = {
    "minife", minife_init, init_argument, NULL,
    NULL,    minife, NULL,          {sizeof(double), 63, 3}};
