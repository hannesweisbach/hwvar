#pragma once

uint64_t read_timebase() {
#if defined(__powerpc64__) || defined(_ARCH_PPC64)
  uint64_t ticks;
  __asm__ volatile("mftb %0" : "=r"(ticks));
  return ticks;
#else
  unsigned int tbl, tbu0, tbu1;
  do {
    __asm__ volatile("mftbu %0" : "=r"(tbu0));
    __asm__ volatile("mftb %0" : "=r"(tbl));
    __asm__ volatile("mftbu %0" : "=r"(tbu1));
  } while (tbu0 != tbu1);
  return (((uint64_t)tbu0) << 32) | (uint64_t)tbl;
#endif
}

static inline uint64_t arch_timestamp_begin(void) { return read_timebase(); }
static inline uint64_t arch_timestamp_end(void) { return read_timebase(); }

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

