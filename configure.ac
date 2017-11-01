AC_PREREQ([2.69])
AC_INIT(hwvar, 1.0, hannes.weisbach@riken.jp)

AC_CANONICAL_TARGET

AM_INIT_AUTOMAKE([foreign])

AC_CONFIG_MACRO_DIRS([m4])

AC_PROG_CC
AC_PROG_RANLIB

AC_HEADER_ASSERT
AC_CHECK_HEADER([stdlib.h], [], [AC_MSG_ERROR([stdlib.h not found.])])
AC_CHECK_HEADER([errno.h], [], [AC_MSG_ERROR([errno.h not found.])])
AC_CHECK_HEADER([pthread.h], [], [AC_MSG_ERROR([pthread.h not found.])])
AC_CHECK_HEADER([hwloc.h], [], [AC_MSG_ERROR([hwloc.h not found.])])
AC_CHECK_HEADER([limits.h], [], [AC_MSG_ERROR([limits.h not found.])])
AC_CHECK_HEADER([sys/time.h], [], [AC_MSG_ERROR([sys/time.h not found.])])
AC_CHECK_HEADERS([sched.h])

AC_CHECK_FUNCS([clock_gettime], [], [AC_MSG_ERROR([clock_gettime() not found.])])
AC_CHECK_FUNCS([memset], [], [AC_MSG_ERROR([memset() not found.])])
AC_CHECK_FUNCS([strerror], [], [AC_MSG_ERROR([strerror() not found.])])
AC_CHECK_FUNCS([strtoul], [], [AC_MSG_ERROR([strtoul() not found.])])

AC_CHECK_TYPES([pthread_barrier_t], [], [], [[#include <pthread.h>]])

AC_CHECK_FUNCS([sched_setaffinity])
AC_SEARCH_LIBS([pow], [m], [], [AC_MSG_ERROR([libm not found.])])
AC_SEARCH_LIBS([hwloc_topology_init], [hwloc], [], [
  AC_MSG_ERROR([unable to find the hwloc_topology_init() function])
])

#AC_C_RESTRICT
AX_CHECK_COMPILE_FLAG([-std=c11], [CFLAGS="$CFLAGS -std=c11"])
AX_CHECK_COMPILE_FLAG([-Weverything], [CFLAGS="$CFLAGS -Weverything"])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_FILES([Makefile
                 src/Makefile
                 src/benchmarks/Makefile])

AC_OUTPUT
