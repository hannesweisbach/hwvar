#pragma once

#include <stdint.h>

static inline uint64_t arch_timestamp_begin(void);
static inline uint64_t arch_timestamp_end(void);

struct pmu;

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu);
static void arch_pmu_free(struct pmu *pmus);
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data);
static void arch_pmu_end(struct pmu *pmus, uint64_t *data);

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

#elif defined(__sparc)

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
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <jevents.h>
#include <rdpmc.h>
#include <mckernel.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>

static uint64_t rdpmc(const uint32_t counter) {
  uint32_t low, high;
  __asm__ volatile("rdpmc" : "=a"(low), "=d"(high) : "c"(counter));
  return (uint64_t)high << 32 | low;
}

static uint64_t rdmsr(const uint32_t idx, const int fd) {
  if (mck_is_mckernel()) {
    return syscall(850, idx);
  } else {
    uint64_t v;
    const ssize_t ret = pread(fd, &v, sizeof(v), idx);
    if (ret != sizeof(v)) {
      printf("Reading MSR failed\n");
    }
    return v;
  }
}

static uint64_t wrmsr(const uint32_t idx, const uint64_t val, const int fd) {
  if (mck_is_mckernel()) {
    return syscall(851, idx, val);
  } else {
    const ssize_t ret = pwrite(fd, &val, sizeof(val), idx);
    if (ret != sizeof(val)) {
      printf("Writing MSR %x failed\n", idx);
    }
    return ret;
  }
}

enum msrs {
  IA32_PMC_BASE = 0x0c1,
  IA32_PERFEVTSEL_BASE = 0x186,
  IA32_FIXED_CTR_CTRL = 0x38d,
  IA32_PERF_GLOBAL_STATUS = 0x38e,
  IA32_PERF_GLOBAL_CTRL = 0x38f,
  IA32_PERF_GLOBAL_OVF_CTRL = 0x390,
  IA32_PERF_CAPABILITIES = 0x345,
  IA32_DEBUGCTL = 0x1d9,
};

static inline void cpuid(const int code, uint32_t *a, uint32_t *b, uint32_t *c,
                         uint32_t *d) {
  asm volatile("cpuid" : "=a"(*a), "=b"(*b), "=c"(*c), "=d"(*d) : "a"(code));
}

#define MASK(v, high, low) ((v >> low) & ((1 << (high - low + 1)) - 1))

struct pmu {
  struct rdpmc_ctx *ctx;
  int *perf_fds;
  unsigned active;
  int mckernel;
  char *msr;
  int msr_fd;
  uint64_t global_ctrl;
};

static void pmu_info(struct pmu *pmus) {
  uint32_t eax, ebx, ecx, edx;

  cpuid(0x1, &eax, &ebx, &ecx, &edx);

  printf("EAX: %08x\n", eax);
  printf("EBX: %08x\n", ebx);
  printf("ECX: %08x\n", ecx);
  printf("EDX: %08x\n", edx);

  if (ecx & (1 << 15)) {
    const uint64_t caps = rdmsr(IA32_PERF_CAPABILITIES, pmus->msr_fd);
    const int vmm_freeze = MASK(caps, 12, 12);
    printf("Caps: %08x\n", caps);
    printf("VMM Freeze: %u\n", vmm_freeze);
    if (vmm_freeze) {
      const uint64_t debugctl = rdmsr(IA32_DEBUGCTL, pmus->msr_fd);
      wrmsr(IA32_DEBUGCTL, debugctl & ~(1 << 14), pmus->msr_fd);
    }
  }

  cpuid(0xa, &eax, &ebx, &ecx, &edx);

  const unsigned version = MASK(eax, 7, 0);
  const unsigned counters = MASK(eax, 15, 8);
  const unsigned width = MASK(eax, 23, 16);

  const unsigned ffpc = MASK(edx, 4, 0);
  const unsigned ff_width = MASK(edx, 12, 5);

  printf("EAX: %08x\n", eax);
  printf("EBX: %08x\n", ebx);
  printf("ECX: %08x\n", ecx);
  printf("EDX: %08x\n", edx);
  printf("PMC version %u\n", version);
  printf("PMC counters: %u\n", counters);
  printf("PMC width: %u\n", width);

  printf("FFPCs: %u, width: %u\n", ffpc, ff_width);
}

#define LINUX_JEVENTS
//#define LINUX_PERF
//#define LINUX_RAWMSR
#define MCK_MCK
//#define MCK_RAWMSR

#if defined(LINUX_RAWMSR) || defined(MCK_RAWMSR)
#define RAWMSR
#endif

#if (defined(LINUX_JEVENTS) + defined(LINUX_PERF) + defined(LINUX_RAWMSR)) > 1
#error "More than one PMU method for Linux selected."
#endif

#if (defined(MCK_MCK) + defined(MCK_RAWMSR)) > 1
#error "More than one PMU method for McKernel selected."
#endif

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  struct pmu *pmus = (struct pmu *)malloc(sizeof(struct pmu));
  pmus->ctx = (struct rdpmc_ctx *)malloc(sizeof(struct rdpmc_ctx) * num_pmcs);
  pmus->perf_fds = (int *)malloc(sizeof(int) * num_pmcs);
  pmus->active = 0;
  pmus->mckernel = mck_is_mckernel();
  asprintf(&pmus->msr, "/dev/cpu/%u/msr", cpu);

#if defined(RAWMSR)
  if (!mck_is_mckernel()) {
    pmus->msr_fd = open(pmus->msr, O_RDWR);
    if (pmus->msr_fd < 0) {
      printf("Opening MSR failed: %d %s\n", errno, strerror(errno));
      exit(EXIT_FAILURE);
    }
  }

  pmu_info(pmus);
#endif

#if defined(MCK_MCK) || defined(RAWMSR)
  pmus->global_ctrl = rdmsr(IA32_PERF_GLOBAL_CTRL, -1);
#endif

  for (unsigned i = 0; i < num_pmcs; ++i) {
    struct perf_event_attr attr;
    const char *event = pmcs[i];
    int err = resolve_event(event, &attr);
    if (err) {
      printf("Error resolving event: \"%s\".\n", event);
      continue;
    }

    if (pmus->mckernel) {
#if defined(MCK_MCK)
      err = mck_pmc_init(pmus->active, attr.config, 0x4);
      if (err) {
        printf("Error configuring PMU with \"%s\" %x\n", event, attr.config);
        continue;
      }
#elif defined(MCK_RAWMSR)
      const uint64_t v = (1 << 22) | (1 << 16) | attr.config;
      wrmsr(IA32_PERFEVTSEL_BASE + i, v, pmus->msr_fd);
#else
      fprintf(stderr, "PMU method not implmented in McKernel.\n");
#endif
    } else {
#if defined(LINUX_JEVENTS)
      err = rdpmc_open_attr(&attr, &pmus->ctx[pmus->active], NULL);
      if (err) {
        printf("Error opening RDPMC context for event \"%s\"\n", event);
        continue;
      }
#elif defined(LINUX_PERF)
      struct perf_event_attr pe;
      memset(&pe, 0, sizeof(pe));
      pe.config = attr.config;
      pe.config1 = attr.config1;
      pe.config2 = attr.config2;
      pe.type = attr.type;
      pe.size = attr.size;
      // attr.disabled = 1;
      pe.exclude_kernel = 1;
      pe.exclude_hv = 1;
      err = perf_event_open(&attr, 0, -1, -1, 0);
      if (err < 0) {
        printf("perf_event_open failed \"%s\" %d %s\n", event, errno , strerror(errno));
        continue;
      }
      pmus->perf_fds[pmus->active] = err;
#elif defined(LINUX_RAWMSR)
      const uint64_t v = (1 << 22) | (1 << 16) | attr.config;
      wrmsr(IA32_PERFEVTSEL_BASE + i, v, pmus->msr_fd);
#else
      fprintf(stderr, "PMU method not implemented in Linux.\n");
#endif
    }

    ++pmus->active;
  }

  if (pmus->mckernel) {
#if defined(MCK_RAWMSR)
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, pmus->msr_fd);
#elif defined(MCK_MCK)
    /* If PMUs are deactivated activate them now.
     * McKernel only initializes them on boot.
     * If, for example RAWMSR turns them off, they stay off.
     */
    if (pmus->global_ctrl == 0) {
      wrmsr(IA32_PERF_GLOBAL_CTRL, (1 << pmus->active) - 1, -1);
    }
#endif
  } else {
#if defined(LINUX_RAWMSR)
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, pmus->msr_fd);
#endif
  }

  return pmus;
}

static void arch_pmu_free(struct pmu *pmus) {
  if (!pmus->mckernel) {
    for (unsigned i = 0; i < pmus->active; ++i) {
#if defined(LINUX_JEVENTS)
      rdpmc_close(&pmus->ctx[i]);
#elif defined(LINUX_PERF)
      close(pmus->perf_fds[i]);
#elif defined(LINUX_RAWMSR)
      wrmsr(IA32_PERFEVTSEL_BASE + i, 0, pmus->msr_fd);
#endif
    }

#if defined(LINUX_RAWMSR)
    wrmsr(IA32_PERF_GLOBAL_CTRL, pmus->global_ctrl, pmus->msr_fd);
    close(pmus->msr_fd);
#endif
  } else {
    wrmsr(IA32_PERF_GLOBAL_CTRL, pmus->global_ctrl, pmus->msr_fd);
  }

  free(pmus->ctx);
  free(pmus->perf_fds);
  free(pmus->msr);
  free(pmus);
}

static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {
  if (pmus->mckernel) {
    for (unsigned i = 0; i < pmus->active; ++i) {
#if defined(MCK_MCK)
      mck_pmc_reset(i);
#elif defined(MCK_RAWMSR)
      wrmsr(IA32_PMC_BASE + i, 0, -1);
#endif
    }

#if defined(MCK_RAWMSR)
    const uint64_t mask = (1 << pmus->active) - 1;
    wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, pmus->msr_fd);
    wrmsr(IA32_PERF_GLOBAL_CTRL, mask, pmus->msr_fd);
#endif
  } else {
    for (unsigned i = 0; i < pmus->active; ++i) {
#if defined(LINUX_JEVENTS)
      data[i] = rdpmc_read(&pmus->ctx[i]);
#elif defined(LINUX_PERF)
      if (ioctl(pmus->perf_fds[i], PERF_EVENT_IOC_RESET, 0)) {
        printf("Error in ioctl\n");
      }
#elif defined(LINUX_RAWMSR)
      wrmsr(IA32_PMC_BASE + i, 0, pmus->msr_fd);
#endif
    }

#if defined(LINUX_RAWMSR)
    const uint64_t mask = (1 << pmus->active) - 1;
    wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, pmus->msr_fd);
    wrmsr(IA32_PERF_GLOBAL_CTRL, mask, pmus->msr_fd);
#endif
  }
}

static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {
  if (pmus->mckernel) {
#if defined(MCK_RAWMSR)
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, pmus->msr_fd);
    const uint64_t ovf = rdmsr(IA32_PERF_GLOBAL_STATUS, pmus->msr_fd);
    if (ovf) {
      printf("OVF: %08x\n", ovf);
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, pmus->msr_fd);
    }
#endif
  } else {
#if defined(LINUX_RAWMSR)
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, pmus->msr_fd);
    const uint64_t ovf = rdmsr(IA32_PERF_GLOBAL_STATUS, pmus->msr_fd);
    if (ovf) {
      printf("OVF: %08x\n", ovf);
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, pmus->msr_fd);
    }
#endif
  }

  for (unsigned i = 0; i < pmus->active; ++i) {
    if (pmus->mckernel) {
#if defined(MCK_MCK)
      data[i] = rdpmc(i);
#elif defined(MCK_RAWMSR)
      data[i] = rdmsr(IA32_PMC_BASE + i, pmus->msr_fd);
#endif
    } else {
#if defined(LINUX_JEVENTS)
      data[i] = rdpmc_read(&pmus->ctx[i]) - data[i];
#elif defined(LINUX_PERF)
      read(pmus->perf_fds[i], &data[i], sizeof(long long));
#elif defined(LINUX_RAWMSR)
      data[i] = rdmsr(IA32_PMC_BASE + i, pmus->msr_fd);
#endif
    }
  }
}

#else /* JEVENTS_FOUND */

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
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

#elif defined(__bgq__)

#include <hwi/include/bqc/A2_inlines.h>

static inline uint64_t arch_timestamp_begin(void) { return GetTimeBase(); }
static inline uint64_t arch_timestamp_end(void) { return GetTimeBase(); }

#ifdef HAVE_BGPM

#include <bgpm/include/bgpm.h>

struct pmu_evt {
  const char *name;
  unsigned id;
};

struct pmu_evt events[] = {
    {"PEVT_LSU_COMMIT_LD_MISSES", PEVT_LSU_COMMIT_LD_MISSES},
    {"PEVT_L2_MISSES", PEVT_L2_MISSES}};

int find_event(const char *const name, unsigned *id) {
  const unsigned elems = sizeof(events) / sizeof(struct pmu_evt);
  for (unsigned i = 0; i < elems; ++i) {
    if (strcasecmp(name, events[i].name) == 0) {
      if (id) {
        *id = events[i].id;
      }
      return 0;
    }
  }
  return -1;
}

struct pmu {
  int evt_set;
};

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  if (num_pmcs == 0) {
    return NULL;
  }

  if (Bgpm_Init(BGPM_MODE_SWDISTRIB)) {
    perror("Bgpm_Iinit()");
    exit(EXIT_FAILURE);
  }

  struct pmu *pmu = (struct pmu *)malloc(sizeof(struct pmu));
  if (pmu == NULL) {
    fprintf(stderr, "Allocating struct pmu failed\n");
    exit(EXIT_FAILURE);
  }

  pmu->evt_set = Bgpm_CreateEventSet();
  if (pmu->evt_set < 0) {
    fprintf(stderr, "Bgpm_CreateEventSet() failed: %d\n", pmu->evt_set);
    exit(EXIT_FAILURE);
  }

  for (unsigned i = 0; i < num_pmcs; ++i) {
    unsigned id;
    int err = find_event(pmcs[i], &id);
    if (err < 0) {
      fprintf(stderr, "Event %s not found.\n", pmcs[i]);
      continue;
    }

    err = Bgpm_AddEvent(pmu->evt_set, id);
    if (err < 0) {
      fprintf(stderr, "Bgpm_AddEvent() failed: %d\n", err);
      exit(EXIT_FAILURE);
    }
  }

  const int err = Bgpm_Apply(pmu->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Apply() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  return pmu;
}

static void arch_pmu_free(struct pmu *pmus) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_DeleteEventSet(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_DeleteEventSet() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_Disable();
  if (err < 0) {
    fprintf(stderr, "Bgpm_Disable() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  free(pmus);
}

static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_Reset(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Reset() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_Start(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Start() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }
}

static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {
  if (pmus == NULL) {
    return;
  }

  int err = Bgpm_Stop(pmus->evt_set);
  if (err < 0) {
    fprintf(stderr, "Bgpm_Stop() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }

  err = Bgpm_ReadEvent(pmus->evt_set, 0, data);
  if (err < 0) {
    fprintf(stderr, "Bgpm_ReadEvent() failed: %d\n", err);
    exit(EXIT_FAILURE);
  }
}

#else /* HAVE_BGPM */

static struct pmu *arch_pmu_init(const char **pmcs, const unsigned num_pmcs,
                                 const unsigned cpu) {
  return NULL;
}
static void arch_pmu_free(struct pmu *pmus) {}
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data) {}
static void arch_pmu_end(struct pmu *pmus, uint64_t *data) {}

#endif /* HAVE_BGPM */

#elif defined(__ppc__) || defined(_ARCH_PPC) || defined(__PPC__)

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

#else

#error "Unkown/Unsupported architecture"

#endif
