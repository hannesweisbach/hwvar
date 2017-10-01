#include <sys/types.h>
#include <sys/sysctl.h>

#include <platform.h>

static int get_sysctl_key(const char *const name) {
  int count;
  size_t count_len = sizeof(count);
  int ret = sysctlbyname(name, &count, &count_len, NULL, 0);
  return ret ? ret : count;
}

int get_number_cpus() { return get_sysctl_key("hw.logicalcpu"); }
int get_current_cpu() { return get_sysctl_key("hw.activecpu"); }
