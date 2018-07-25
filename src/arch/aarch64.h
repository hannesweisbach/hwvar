#pragma once

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

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
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

