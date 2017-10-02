#include <arch.h>

uint64_t arch_timestamp_begin(void) {
  unsigned high, low;
  __asm__ volatile("CPUID\n\t"
                   "RDTSC\n\t"
                   "mov %%edx, %0\n\t"
                   "mov %%eax, %1\n\t"
                   : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");

  return (uint64_t)high << 32ULL | low;
}

uint64_t arch_timestamp_end(void) {
  unsigned high, low;
  __asm__ volatile("RDTSCP\n\t"
                   "mov %%edx,%0\n\t"
                   "mov %%eax,%1\n\t"
                   "CPUID\n\t"
                   : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");
  return (uint64_t)high << 32ULL | low;
}
