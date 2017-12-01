SUBDIRS = benchmarks
AM_CFLAGS = -std=c99
AM_CPPFLAGS = -D_GNU_SOURCE
AM_CPPFLAGS += -I$(top_srcdir)/include
AM_CPPFLAGS += -I$(srcdir)/benchmarks
# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
noinst_LIBRARIES = libbarrier.a libworker.a
libbarrier_a_SOURCES = barrier.c barrier.h
libworker_a_SOURCES = worker.c

bin_PROGRAMS = hwvar
hwvar_SOURCES = main.c
nodist_EXTRA_hwvar_SOURCES = dummy.cxx # to make automake link with g++
hwvar_LDADD = libworker.a libbarrier.a benchmarks/libbenchmarks.a
hwvar_LDFLAGS = -pthread
