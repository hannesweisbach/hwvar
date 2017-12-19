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
  const char *name;
  uint64_t type;
};

/* ARM ARM D5.10 */
const struct pmu_event pmu_events[] = {
    {"SW_INCR", 0x00},          {"L1I_CACHE_REFILL", 0x01},
    {"L1D_CACHE_REFILL", 0x03}, {"L1D_CACHE", 0x04},
    {"MEM_ACCESS", 0x13},       {"L2D_CACHE", 0x16},
    {"L2D_CACHE_REFILL", 0x17}, {"CHAIN", 0x1e},
};

struct pmu {
  unsigned num_pmcs;
};

static void dmb() { __asm__ volatile("dmb sy" ::: "memory"); }
static void isb() { __asm__ volatile("isb"); }

static void select_reg(const uint64_t reg) {
  __asm__ volatile("msr PMSELR_EL0, %0" : : "r"(reg));
}

static void enable_reg(const uint32_t reg) {
  __asm__ volatile("msr PMCNTENSET_EL0, %0" : : "r"((uint64_t)1 << reg));
}

static void disable_reg(const uint32_t reg) {
  __asm__ volatile("msr PMCNTENCLR_EL0, %0" : : "r"((uint64_t)1 << reg));
}

static uint64_t read_current_counter() {
  uint64_t value;
  __asm__ volatile("mrs %0, PMXEVCNTR_EL0" : "=r"(value));
  return value;
}

static void write_type(const uint64_t type) {
  uint64_t result = 0x08000000 | (type & 0xffff);
  __asm__ volatile("msr PMXEVTYPER_EL0, %0" : : "r"(result));
}

static int str2type(const char *name, uint64_t *out) {
  for (unsigned i = 0; i < sizeof(pmu_events) / sizeof(pmu_events[0]); ++i) {
    if (strcasecmp(name, pmu_events[i].name) == 0) {
      if (out) {
        *out = pmu_events[i].type;
      }
      return 0;
    }
  }
  return -1;
}

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  uint64_t value;

  __asm__ volatile("mrs %0, PMCR_EL0" : "=r"(value));

  if (!(value & 1)) {
    fprintf(stderr, "PMU not enabled\n");
    exit(EXIT_FAILURE);
  }

  const unsigned num_counters = (value >> 11) & 0x1f;
  if (num_counters < num_pmcs) {
    fprintf(stderr,
            "%u events requested but only %u event counters are available.\n",
            num_pmcs, num_counters);
  }

  struct pmu *pmu = (struct pmu *)malloc(sizeof(struct pmu));
  pmu->num_pmcs = 0;

  for (unsigned pmc = 0; pmc < num_pmcs && pmc < num_counters; ++pmc) {
    uint64_t type;
    const char *name = pmcs[pmc];
    const int err = str2type(name, &type);
    if (err) {
      fprintf(stderr, "PMC %s not found.\n", name);
      continue;
    }
    select_reg(pmu->num_pmcs);
    write_type(type);
    pmu->num_pmcs = pmu->num_pmcs + 1;
  }

  for (int i = (int)pmu->num_pmcs; i > 0; --i) {
    enable_reg((unsigned)(i - 1));
    isb();
  }

  return pmu;
}

static void arch_pmu_free(struct pmu *pmus) {
  for (unsigned i = 0; i < pmus->num_pmcs; ++i) {
    select_reg(i);
    disable_reg(i);
  }

  free(pmus);
}

static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {
  for (unsigned i = 0; i < pmus->num_pmcs; ++i) {
    // clear overflow flag
    __asm__ volatile("msr PMOVSCLR_EL0, %0" : : "r"((uint64_t)1 << i));
    select_reg(i);
    // set counter register to 0
    __asm__ volatile("msr PMXEVCNTR_EL0, %0" : : "r"((uint64_t)0));
  }

  isb();
}

static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {
  uint64_t ovf = 0;
  __asm__ volatile("mrs %0, PMOVSSET_EL0" : "=r"(ovf));

  for (unsigned i = 0; i < pmus->num_pmcs; ++i) {
    select_reg(i);
    *data++ = read_current_counter();
    if (ovf & (1 << i)) {
      printf("Warning, overflow in counter %d\n", i);
    }
  }
}

#elif defined(__sparc)

static inline uint64_t timestamp() {
  uint64_t value;
  __asm__ volatile("rd %%tick, %0\n" : "=r"(value));
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

#ifdef JEVENTS_FOUND 
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
  }
}

#else /* JEVENTS_FOUND */

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  if (num_pmcs) {
    fprintf(stderr, "PMC support not compiled in.\n");
    exit(EXIT_FAILURE);
  }
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#endif /* JEVENTS_FOUND */

#elif defined(_ARCH_QP)

#include <sys/time.h>

uint64_t read_timebase() {
  timebasestruct_t tb;
  read_real_time(&tb, TIMEBASE_SZ);
  return (uint64_t)tb.tb_high << 32 | tb.tb_low;
}

static inline uint64_t arch_timestamp_begin(void) { return read_timebase(); }
static inline uint64_t arch_timestamp_end(void) { return read_timebase(); }

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#else

#error "Unkown/Unsupported architecture"

#endif
