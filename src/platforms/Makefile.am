AM_CFLAGS  = -D_GNU_SOURCE
AM_CFLAGS += -I $(top_srcdir)/include
noinst_LIBRARIES = libplatform.a
if LINUX
libplatform_a_SOURCES = linux.c
endif
if MACOS
libplatform_a_SOURCES = macos.c
endif
