#pragma once

#include <stdint.h>

static inline uint64_t arch_timestamp_begin(void);
static inline uint64_t arch_timestamp_end(void);

struct pmu;

#ifdef __aarch64__

static inline uint64_t timestamp() {
  uint64_t value;
  // Read CCNT Register
  __asm__ volatile("mrs %0, cntvct_EL0\t\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

struct pmu_event {
  const char *name, uint64_t type
};

const struct pmu_event pmu_events[] = {

    {"SW_INCR", 0x00},
#if 0
   "L1I_CACHE_REFILL", "L1D_CACHE_REFILL", "L1D_CACHE",
    "MEM_ACCESS", "L2D_CACHE",        "L2D_CACHE_REFILL", "CHAIN",
#endif
};

/* ARM ARM D5.10 */
enum pmu_events {
  SW_INCR = 0x00,
  L1I_CACHE_REFILL = 0x01,
  L1D_CACHE_REFILL = 0x03,
  L1D_CACHE = 0x04,
  MEM_ACCESS = 0x13,
  L2D_CACHE = 0x16,
  L2D_CACHE_REFILL = 0x17,
  CHAIN = 0x1e,
};

static void select_reg(const uint64_t reg) {
  __asm__ volatile("msr PMSELR_EL0, %0" : : "r"(reg));
}

static void enable_reg(const uint32_t reg) {
  __asm__ volatile("msr PMCNTENSET_EL0, %0" : : "r"((uint64_t)1 << reg));
}

static void write_type(const uint64_t type) {
  uint64_t result = 0x08000000 | (type & 0xffff);
  __asm__ volatile("msr PMXEVTYPER_EL0, %0" : : "r"(result));
}

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
static void *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  static unsigned type[] = {
#ifdef L1_I_MISS
    L1I_CACHE_REFILL,
#endif
#ifdef L1_D_MISS
    L1D_CACHE_REFILL,
#endif
#ifdef L2_D_MISS
    L2D_CACHE_REFILL,
#endif
#ifdef L2_D_REF
    L2D_CACHE,
#endif
#ifdef L1_D_REF
    L1D_CACHE,
#endif
#if 0
    CHAIN
#endif
  };
  const uint64_t num_registers = sizeof(type) / sizeof(type[0]);
  uint64_t value;

  __asm__ volatile("mrs %0, PMCR_EL0" : "=r"(value));

  if (!(value & 1)) {
    fprintf(stderr, "PMU not enabled\n");
    exit(EXIT_FAILURE);
  }

  for (uint64_t reg = 0; reg < num_registers; ++reg) {
    select_reg(reg);
    write_type(type[reg]);
  }

  for (uint64_t reg = num_registers; reg > 0; --reg) {
    enable_reg(reg - 1);
    __asm__ volatile("isb");
  }
  return NULL;
}

static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#elif defined(__sparc)

static inline uint64_t timestamp() {
  uint64_t value;
  __asm__ volatile("rd %%tick %0\n" : "=r"(value));
  return value;
}

static inline uint64_t arch_timestamp_begin(void) { return timestamp(); }
static inline uint64_t arch_timestamp_end(void) { return timestamp(); }

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

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
  struct rdpmc_ctx *ctx;
  unsigned active;
};

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  struct pmu *pmus = (struct pmu *)malloc(sizeof(struct pmu));
  pmus->ctx = (struct rdpmc_ctx *)malloc(sizeof(struct rdpmc_ctx) * num_pmcs);
  pmus->active = 0;

  for (unsigned i = 0; i < num_pmcs; ++i) {
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

static void arch_pmu_free(struct pmu *pmus) {
  for (unsigned i = 0; i < pmus->active; ++i) {
    rdpmc_close(&pmus->ctx[i]);
  }

  free(pmus->ctx);
  free(pmus);
}

static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {
  for (unsigned i = 0; i < pmus->active; ++i) {
    data[i] = rdpmc_read(&pmus->ctx[i]);
  }
}

static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {
  for (unsigned i = 0; i < pmus->active; ++i) {
    data[i] = rdpmc_read(&pmus->ctx[i]) - data[i];
    data[i] = i + 1;
  }
}

#else /* HAVE_RDPMC_H */

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#endif /* HAVE_RDPMC_H */

#else

#error "Unkown/Unsupported architecture"

#endif
