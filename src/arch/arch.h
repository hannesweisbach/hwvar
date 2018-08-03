#pragma once

#include <memory>
#include <vector>

#include <stdint.h>

#include <gsl/gsl>

namespace arch {

template <typename ARCH>
class arch_pmu_base {
private:
  ptrdiff_t offset_ = 0;
  const ptrdiff_t slice_;
  gsl::span<uint64_t> span_;
  ARCH arch_;

  auto current_pmu_span() { return span_.subspan(offset_ + 1, slice_ - 1); }
  auto &&current_time() { return span_.subspan(offset_, 1)[0]; }

public:
  arch_pmu_base(const pmc &pmcs, gsl::span<uint64_t> output)
      : slice_(pmcs.size()), span_(output), arch_(pmcs) {}

  void start() {
    auto pmu_span = current_pmu_span();
    arch_.start(pmu_span.begin());
    current_time() = arch_.timestamp_begin();
  }

  void stop() {
    current_time() = arch_.timestamp_end() - current_time();
    auto pmu_span = current_pmu_span();
    arch_.stop(pmu_span.begin());
    offset_ += slice_;
  }
};

}

#ifdef __aarch64__
#  include "aarch64.h"
#elif defined(__sparc)
#  include "sparc.h"
#elif defined(__x86_64__)
#  include "x86_64.h"

/* MCK can be either:
 * - mck_rawmsr for raw MSR access via rdmsr/wrmsr mcK syscalls
 * - mck_mckmsr for PMU programmed via mcK pmc_* syscalls
 *
 * LINUX can be any of:
 * - linux_perf for using perf
 * - linux_jevents for using jevents
 * - linux_rawmsr for raw MSR access using /dev/cpu/N/msr
 */

using pmu = arch::arch_pmu_base<x86_pmu<mck_mckmsr, linux_rawmsr>>;
using event = struct perf_event_attr;

#elif defined(__bgq__)
#  include "bgq.h"
#elif defined(__ppc__) || defined(_ARCH_PPC) || defined(__PPC__)
#  include "ppc.h"
#else

#error "Unkown/Unsupported architecture"

#endif

