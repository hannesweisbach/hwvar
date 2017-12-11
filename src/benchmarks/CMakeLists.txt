if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/HPCCG/.git OR
   NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/MiniFE/.git)
  message(STATUS "Running git submodule update --init.")
  execute_process(COMMAND git submodule update --init WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})
endif()

set(HPCCG_SRC HPCCG/generate_matrix.cpp HPCCG/compute_residual.cpp
              HPCCG/dump_matlab_matrix.cpp HPCCG/HPC_sparsemv.cpp
              HPCCG/HPCCG.cpp HPCCG/waxpby.cpp HPCCG/ddot.cpp)
#Main: HPCCG/main.cpp HPCCG/read_HPC_row.cpp
#YAML: HPCCG/YAML_Element.cpp HPCCG/YAML_Doc.cpp
#MPI: HPCCG/make_local_matrix.cpp HPCCG/exchange_externals.cpp HPCCG/mytimer.cpp

add_library(MiniFE minife.cpp
  MiniFE/ref/utils/param_utils.cpp
  MiniFE/ref/utils/utils.cpp
  MiniFE/ref/utils/BoxPartition.cpp
)
set_property(TARGET MiniFE APPEND PROPERTY INCLUDE_DIRECTORIES
  ${CMAKE_CURRENT_SOURCE_DIR}/MiniFE/ref/utils
  ${CMAKE_CURRENT_SOURCE_DIR}/MiniFE/ref/src
  ${CMAKE_CURRENT_SOURCE_DIR}/MiniFE/ref/fem
)
set_property(TARGET MiniFE APPEND PROPERTY COMPILE_DEFINITIONS
  MINIFE_SCALAR=double
  MINIFE_LOCAL_ORDINAL=int
  MINIFE_GLOBAL_ORDINAL=int
  MINIFE_CSR_MATRIX
# MINIFE_ELL_MATRIX
  MINIFE_KERNELS=0
)

add_library(benchmark benchmark.c dgemm.c sha256.c HACCmk.c stream.c fwq.c capacity.cpp hpccg.cpp ${HPCCG_SRC})
target_include_directories(benchmark PUBLIC .)
target_link_libraries(benchmark LINK_PUBLIC MiniFE)
