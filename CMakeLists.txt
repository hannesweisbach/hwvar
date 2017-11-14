cmake_minimum_required(VERSION 2.8.11)
project(HWPErfVar C)

if ("${CMAKE_VERSION}" VERSION_EQUAL "3.1" OR
    "${CMAKE_VERSION}" VERSION_GREATER "3.1")
  set(CMAKE_C_STANDARD 99)
  set(CMAKE_C_STANDARD_REQUIRED ON)
else()
  if(CMAKE_COMPILER_IS_GNUCC OR CMAKE_COMPILER_IS_CLANG)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu99")
  else()
    message(STATUS "C compiler is neither clang nor gcc.")
    message(STATUS "Please enable C99 mode manually via CFLAGS for your compiler.")
    message(STATUS "If it is already enable ignore this message.")
  endif()
endif()

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${CMAKE_SOURCE_DIR}/cmake)

include(CheckIncludeFiles)
include(CheckTypeSize)

check_include_files(sched.h HAVE_SCHED_H)
check_type_size(pthread_barrier_t HAVE_PTHREAD_BARRIER_T LANGUAGE C)

find_package(HWLOC REQUIRED)

include_directories(include ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(src)

configure_file(cmake-config.h.in config.h)
