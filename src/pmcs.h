#pragma once

#include <upca/upca.h>

#ifdef JEVENTS_FOUND
using backend_type =
    upca::arch::arch_common_base<upca::arch::x86_64::x86_linux_mckernel<
        upca::arch::x86_64::mck_mckmsr, upca::arch::x86_64::linux_jevents>>;
// upca::resolver<backend_type> upca;
using pmc = upca::resolver<backend_type>;
#else
using backend_type = upca::arch::arch_common_base<upca::arch::x86_64::x86_64_pmu>;
using pmc = upca::resolver<backend_type>;
#endif

