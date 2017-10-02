AM_CFLAGS  = -D_GNU_SOURCE -O3 -std=c11
AM_CFLAGS += -I $(top_srcdir)/include
noinst_LIBRARIES = libarch.a
if X86_64
libarch_a_SOURCES = x86_64.c
endif

if AARCH64
libarch_a_SOURCES = aarch64.c
endif



