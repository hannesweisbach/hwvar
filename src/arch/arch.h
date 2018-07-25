#pragma once

#include <stdint.h>

static inline uint64_t arch_timestamp_begin(void);
static inline uint64_t arch_timestamp_end(void);

struct pmu;

static struct pmu *arch_pmu_init(const pmc *pmcs, const unsigned cpu);
static void arch_pmu_free(struct pmu *pmus);
static void arch_pmu_begin(struct pmu *pmus, uint64_t *data);
static void arch_pmu_end(struct pmu *pmus, uint64_t *data);

template <typename ARCH>
class arch_pmu_base {
protected:
  std::vector<uint64_t> data_;

public:
  arch_pmu_base(size_t size) : data_(size) {}
};

#ifdef __aarch64__
#  include "aarch64.h"
#elif defined(__sparc)
#  include "sparh.c"
#elif defined(__x86_64__)
#  include "x86_64.h"
#elif defined(__bgq__)
#  include "bgq.h"
#elif defined(__ppc__) || defined(_ARCH_PPC) || defined(__PPC__)
#  include "ppc.h"
#else

#error "Unkown/Unsupported architecture"

#endif
