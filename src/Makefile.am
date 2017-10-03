SUBDIRS = platforms benchmarks arch
AM_CFLAGS  = -D_GNU_SOURCE -Og -std=c11 -g
#AM_CFLAGS += -fsanitize=undefined -fsanitize=thread
AM_CFLAGS += -I $(top_srcdir)/include
AM_CFLAGS += -I $(srcdir)/benchmarks
# this lists the binaries to produce, the (non-PHONY, binary) targets in
# the previous manual Makefile
noinst_LIBRARIES = libbarrier.a libworker.a
libbarrier_a_SOURCES = barrier.c barrier.h
libworker_a_SOURCES = worker.c

bin_PROGRAMS = hwvar
hwvar_SOURCES = main.c
hwvar_LDADD = libworker.a libbarrier.a benchmarks/libbm_dgemm.a arch/libarch.a platforms/libplatform.a 
hwvar_LDFLAGS = -pthread
