#pragma once

#include <stdint.h>

static inline uint64_t arch_timestamp_begin(void);
static inline uint64_t arch_timestamp_end(void);

#ifdef __aarch64__

static inline uint64_t timestamp() {
  uint64_t value;
  // Read CCNT Register
  __asm__ volatile("mrs %0, cntvct_EL0\t\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

#elif defined(__sparc)

static inline uint64_t timestamp() {
  uint64_t value;
  __asm__ volatile("rd %%tick %0\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

#elif defined(__x86_64__)

static inline uint64_t arch_timestamp_begin(void) {
  unsigned high, low;
  __asm__ volatile("CPUID\n\t"
                   "RDTSC\n\t"
                   "mov %%edx, %0\n\t"
                   "mov %%eax, %1\n\t"
                   : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");

  return (uint64_t)high << 32ULL | low;
}

static inline uint64_t arch_timestamp_end(void) {
  unsigned high, low;
  __asm__ volatile("RDTSCP\n\t"
                   "mov %%edx,%0\n\t"
                   "mov %%eax,%1\n\t"
                   "CPUID\n\t"
                   : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");
  return (uint64_t)high << 32ULL | low;
}

#else

#error "Unkown/Unsupported architecture"

#endif
