set(HPCCG_SRC HPCCG/generate_matrix.cpp HPCCG/compute_residual.cpp
              HPCCG/dump_matlab_matrix.cpp HPCCG/HPC_sparsemv.cpp
              HPCCG/HPCCG.cpp HPCCG/waxpby.cpp HPCCG/ddot.cpp)
#Main: HPCCG/main.cpp HPCCG/read_HPC_row.cpp
#YAML: HPCCG/YAML_Element.cpp HPCCG/YAML_Doc.cpp
#MPI: HPCCG/make_local_matrix.cpp HPCCG/exchange_externals.cpp HPCCG/mytimer.cpp

add_library(benchmark benchmark.c dgemm.c sha256.c HACCmk.c stream.c fwq.c hpccg.c++ ${HPCCG_SRC})
target_include_directories(benchmark PUBLIC .)
