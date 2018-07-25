#pragma once

static inline uint64_t timestamp() {
  uint64_t value;
  __asm__ volatile("rd %%tick, %0\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}


