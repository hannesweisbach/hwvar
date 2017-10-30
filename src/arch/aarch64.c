#include <arch.h>

#if 0
static uint64_t timestamp() {
  uint64_t value;
  // Read CCNT Register
  __asm__ volatile("mrs %0, cntvct_EL0\t\n" : "=r"(value));
  return value;
}

uint64_t arch_timestamp_begin(void) { return timestamp(); }
uint64_t arch_timestamp_end(void) { return timestamp(); }
#endif
