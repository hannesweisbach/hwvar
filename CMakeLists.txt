cmake_minimum_required(VERSION 2.8.11)
project(HWPErfVar C CXX)

if(${CMAKE_C_COMPILER_ID} MATCHES "Intel")
  set(COMPILER_IS_INTEL ON)
  execute_process(COMMAND ${CMAKE_C_COMPILER} --version
	OUTPUT_VARIABLE ICC_VERSION_OUTPUT
	ERROR_VARIABLE  ICC_VERSION_OUTPUT
	RESULT_VARIABLE ICC_VERSION_RESULT)
  if(ICC_VERSION_RESULT)
    message(SEND_ERROR "Command \"${CMAKE_C_COMPILER} --version\" failed:\n${ICC_VERSION_RESULT}")
  else()
    STRING(REGEX REPLACE "icc \\(ICC\\) ([0-9]+)\\.[0-9]+.*" "\\1" ICC_MAJOR  "${ICC_VERSION_OUTPUT}")
    if(${ICC_MAJOR} LESS "12")
      message(FATAL_ERROR "At least icc 12.0 is required.")
    endif()
  endif()
endif()

if ("${CMAKE_VERSION}" VERSION_EQUAL "3.1" OR
    "${CMAKE_VERSION}" VERSION_GREATER "3.1")
  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_STANDARD_REQUIRED ON)
else()
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG OR COMPILER_IS_INTEL)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
  else()
    message(STATUS "C compiler is neither clang nor gcc.")
    message(STATUS "Please enable C99 mode manually via CFLAGS for your compiler.")
    message(STATUS "If it is already enable ignore this message.")
  endif()
endif()

set(THREADS_PREFER_PTHREAD_FLAG ON)
set(CMAKE_THREAD_PREFER_PTHREAD ON)
find_package(Threads REQUIRED)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(CheckIncludeFiles)
include(CheckTypeSize)
include(CheckFunctionExists)

check_include_files(sched.h HAVE_SCHED_H)
check_type_size(pthread_barrier_t HAVE_PTHREAD_BARRIER_T LANGUAGE C)

find_package(HWLOC REQUIRED)

check_library_exists("rt" clock_gettime "" HAVE_LIBRT)
if(HAVE_LIBRT)
  find_library(LIBRT_LIBRARIES rt)
  set(HAVE_CLOCK_GETTIME ON)
else()
  # Under macOS and glibc > 2.16 clock_gettime does not need -lrt
  check_function_exists(clock_gettime HAVE_CLOCK_GETTIME)
endif()

find_path(JEVENTS_INCDIR
  NAMES jevents.h
  HINTS ${JEVENTS_DIR}
  PATH_SUFFIXES "include" "include/jevents")
find_library(JEVENTS_LIBRARIES jevents
  HINTS ${JEVENTS_DIR}
  PATH_SUFFIXES lib lib32 lib64)
include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(JEVENTS DEFAULT_MSG JEVENTS_LIBRARIES JEVENTS_INCDIR)

include_directories(include ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src)

configure_file(cmake-config.h.in config.h)
