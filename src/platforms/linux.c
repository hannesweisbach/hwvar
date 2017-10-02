#include <sched.h>
#include <unistd.h>

#include <platform.h>

int platform_get_number_cpus() { return sysconf(_SC_NPROCESSORS_ONLN); }
int platform_get_current_cpu() { return sched_getcpu(); }
