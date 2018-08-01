#pragma once

#include <memory>

namespace arch {
class pmc_data {
public:
  class data;

  pmc_data(const std::string &name) : self_(std::make_unique<data>(name)) {}
  const data &get() const { return *self_; }

private:
  std::unique_ptr<data> self_;
};
} // namespace arch

#ifdef __aarch64__
#  include "aarch64_lookup.h"
#elif defined(__sparc)
#  include "sparc_lookup.h"
#elif defined(__x86_64__)
#  include "x86_64_lookup.h"
#elif defined(__bgq__)
#  include "bgq_lookup.h"
#elif defined(__ppc__) || defined(_ARCH_PPC) || defined(__PPC__)
#  include "ppc_lookup.h"
#else

#error "Unkown/Unsupported architecture"

#endif

