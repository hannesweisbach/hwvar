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

static void *arch_pmu_init(void) {}
static void arch_pmu_free(void *pmus_) {}
static void arch_pmu_begin(void *pmus_, uint64_t *data) {}
static void arch_pmu_end(void *pmus_, uint64_t *data) {}

#elif defined(__sparc)

static inline uint64_t timestamp() {
  uint64_t value;
  __asm__ volatile("rd %%tick %0\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

static void *arch_pmu_init(void) {}
static void arch_pmu_free(void *pmus_) {}
static void arch_pmu_begin(void *pmus_, uint64_t *data) {}
static void arch_pmu_end(void *pmus_, uint64_t *data) {}

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

#ifdef HAVE_RDPMC_H
#include <jevents.h>
#include <rdpmc.h>

struct pmu {
  struct rdpmc_ctx ctx[NUM_PMCS];
  unsigned active;
};

const char *pmcs[NUM_PMCS] = {PMC_INITIALIZER};

static void *arch_pmu_init(void) {
  struct pmu *pmus = (struct pmu *)malloc(sizeof(struct pmu));
  pmus->active = 0;

  for (int i = 0; i < NUM_PMCS; ++i) {
    struct perf_event_attr attr;
    const char *event = pmcs[i];
    int err = resolve_event(event, &attr);
    if (err) {
      printf("Error resolving event: \"%s\".\n", event);
      continue;
    }

    // attr->sample_type/read_format

    err = rdpmc_open_attr(&attr, &pmus->ctx[pmus->active], NULL);
    if (err) {
      printf("Error opening RDPMC context for event \"%s\"\n", event);
      continue;
    }

    ++pmus->active;
  }

  return pmus;
}

static void arch_pmu_free(void *pmus_) {
  struct pmu *pmus = (struct pmu *)pmus_;

  for (int i = 0; i < pmus->active; ++i) {
    rdpmc_close(&pmus->ctx[i]);
  }

  free(pmus);
}

static void arch_pmu_begin(void *pmus_, uint64_t *data) {
  struct pmu *pmus = (struct pmu *)pmus_;

  for (int i = 0; i < pmus->active; ++i) {
    data[i] = rdpmc_read(&pmus->ctx[i]);
  }
}

static void arch_pmu_end(void *pmus_, uint64_t *data) {
  struct pmu *pmus = (struct pmu *)pmus_;

  for (int i = 0; i < pmus->active; ++i) {
    data[i] = rdpmc_read(&pmus->ctx[i]) - data[i];
    data[i] = i+1;
  }
}

#else /* HAVE_RDPMC_H */

static void *arch_pmu_init(void) {}
static void arch_pmu_free(void *pmus_) {}
static void arch_pmu_begin(void *pmus_, uint64_t *data) {}
static void arch_pmu_end(void *pmus_, uint64_t *data) {}

#endif /* HAVE_RDPMC_H */

#else

#error "Unkown/Unsupported architecture"

#endif
