AUTOMAKE_OPTIONS = subdir-objects

noinst_LIBRARIES = libbenchmarks.a
libbenchmarks_a_SOURCES = benchmark.c dgemm.c HACCmk.c stream.c sha256.c fwq.c hpccg.c++ minife.c++ 
libbenchmarks_a_SOURCES+= HPCCG/generate_matrix.cpp HPCCG/compute_residual.cpp HPCCG/dump_matlab_matrix.cpp HPCCG/HPC_sparsemv.cpp HPCCG/HPCCG.cpp HPCCG/waxpby.cpp HPCCG/ddot.cpp
libbenchmarks_a_SOURCES+= MiniFE/ref/utils/param_utils.cpp MiniFE/ref/utils/utils.cpp  MiniFE/ref/utils/BoxPartition.cpp

libbenchmarks_a_CPPFLAGS = -D_GNU_SOURCE
libbenchmarks_a_CPPFLAGS+= -I$(srcdir)/MiniFE/ref/utils -I$(srcdir)/MiniFE/ref/src -I$(srcdir)/MiniFE/ref/fem
libbenchmarks_a_CPPFLAGS+= -DMINIFE_SCALAR=double -DMINIFE_LOCAL_ORDINAL=int -DMINIFE_GLOBAL_ORDINAL=int -DMINIFE_CSR_MATRIX -DMINIFE_KERNELS=0
