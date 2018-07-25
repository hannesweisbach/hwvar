#pragma once

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
#include <iostream>
#include <memory>
#include <sstream>
#include <string>

#include <gsl/gsl>

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif
#include <jevents.h>
#include <rdpmc.h>
#ifdef __cplusplus
}
#endif
#include <mckernel.h>
#include <sys/ioctl.h>
#include <linux/perf_event.h>



class fd {
  int fd_;

public:
explicit fd(int fd) : fd_(fd){}
  fd(std::nullptr_t = nullptr) : fd_(-1) {}
  ~fd() {
    if (fd_ != -1) {
      close(fd_);
    }
  }
  fd(fd &&l) {
    fd_ = l.fd_;
    l.fd_ = -1;
  }
  explicit operator bool(){return fd_ != -1;}
  explicit operator int() { return fd_; }
  friend bool operator==(const fd &l, const fd &r) { return l.fd_ == r.fd_; }
  friend bool operator!=(const fd &l, const fd &r) { return !(l == r); }
};


static std::unique_ptr<fd> make_perf_fd(struct perf_event_attr &attr) {
  const int fd = perf_event_open(&attr, 0, -1, -1, 0);

  if (fd < 0) {
    std::ostringstream os;
    os << "perf_event_open() failed: " << errno << strerror(errno) << std::endl;
    throw std::runtime_error(os.str());
  }
  return std::make_unique<class fd>(fd);
}

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

static uint64_t rdmsr(const uint32_t idx, const std::unique_ptr<fd> &fd) {
  if (mck_is_mckernel()) {
    return syscall(850, idx);
  } else {
    uint64_t v;
    const ssize_t ret = pread(static_cast<int>(*fd), &v, sizeof(v), idx);
    if (ret != sizeof(v)) {
      printf("Reading MSR failed\n");
    }
    return v;
  }
}

static uint64_t wrmsr(const uint32_t idx, const uint64_t val, const std::unique_ptr<fd>& fd) {
  if (mck_is_mckernel()) {
    return syscall(851, idx, val);
  } else {
    const ssize_t ret = pwrite(static_cast<int>(*fd), &val, sizeof(val), idx);
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

static void pmu_info(const std::unique_ptr<fd> &msr_fd) {
  uint32_t eax, ebx, ecx, edx;

  cpuid(0x1, &eax, &ebx, &ecx, &edx);

  std::cout << std::hex;

  std::cout << "EAX: " << eax << std::endl;
  std::cout << "EBX: " << ebx << std::endl;
  std::cout << "ECX: " << ecx << std::endl;
  std::cout << "EDX: " << edx << std::endl;

  if (ecx & (1 << 15)) {
    const uint64_t caps = rdmsr(IA32_PERF_CAPABILITIES, msr_fd);
    const int vmm_freeze = MASK(caps, 12, 12);
    std::cout << "Caps: " << caps << std::endl;
    std::cout << "VMM Freeze: " << std::boolalpha
              << static_cast<bool>(vmm_freeze) << std::noboolalpha << std::endl;
    if (vmm_freeze) {
      const uint64_t debugctl = rdmsr(IA32_DEBUGCTL, msr_fd);
      wrmsr(IA32_DEBUGCTL, debugctl & ~(1 << 14), msr_fd);
    }
  }

  cpuid(0xa, &eax, &ebx, &ecx, &edx);

  const unsigned version = MASK(eax, 7, 0);
  const unsigned counters = MASK(eax, 15, 8);
  const unsigned width = MASK(eax, 23, 16);

  const unsigned ffpc = MASK(edx, 4, 0);
  const unsigned ff_width = MASK(edx, 12, 5);

  std::cout << "EAX: " << eax << std::endl;
  std::cout << "EBX: " << ebx << std::endl;
  std::cout << "ECX: " << ecx << std::endl;
  std::cout << "EDX: " << edx << std::endl;

  std::cout << std::dec;

  std::cout << "PMC version " << version << std::endl;
  std::cout << "PMC counters: " << counters << std::endl;
  std::cout << "PMC width: " << width << std::endl;

  std::cout << "FFPCs: " << ffpc << ", width: " << ff_width << std::endl;
}

static struct pmu *arch_pmu_init(const pmc *pmcs, const unsigned cpu) {
  struct pmu *pmus = (struct pmu *)malloc(sizeof(struct pmu));
  pmus->ctx = (struct rdpmc_ctx *)malloc(sizeof(struct rdpmc_ctx) * pmcs->size());
  pmus->perf_fds = (int *)malloc(sizeof(int) * pmcs->size());
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

  //for (unsigned i = 0; i < pmcs->size(); ++i) {
  for(const auto & pmc : *pmcs) {
    struct perf_event_attr attr;
    const char *event = pmc.name().c_str();
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

#define LINUX_JEVENTS
//#define LINUX_PERF
//#define LINUX_RAWMSR
//#define MCK_MCK
#define MCK_RAWMSR

#if defined(LINUX_RAWMSR) || defined(MCK_RAWMSR)
#define RAWMSR
#endif

#if (defined(LINUX_JEVENTS) + defined(LINUX_PERF) + defined(LINUX_RAWMSR)) > 1
#error "More than one PMU method for Linux selected."
#endif

#if (defined(MCK_MCK) + defined(MCK_RAWMSR)) > 1
#error "More than one PMU method for McKernel selected."
#endif

template <typename MCK, typename LINUX> class x86_pmu {
//x86_pmu
};

static struct perf_event_attr resolve_pmc(const std::string &name) {
  struct perf_event_attr attr;
  const char *event = name.c_str();
  int err = resolve_event(event, &attr);
  if (err) {
    using namespace std::string_literals;
    throw std::runtime_error("Error resolving event \""s + event + "\"\n"s);
  }
  return attr;
}

struct mck_mck_policy {
  static int init(const int active, const int config) {
    return mck_pmc_init(active, config, 0x4);
  }

  /* writes MSR_IA32_PMC0 + i to 0 */
  static void reset(const int i) { mck_pmc_reset(i); }
  /* sets and clears bits in MSR_PERF_GLOBAL_CTRL */
  static void start(const unsigned long mask) {
    const int err = mck_pmc_start(mask);
    if (err) {
      std::cerr << "Error starting PMCs" << std::endl;
    }
  }
  static void stop(const unsigned long mask) {
    const int err = mck_pmc_stop(mask);
    if (err) {
      std::cerr << "Error stopping PMCs" << std::endl;
    }
  }
  static uint64_t read(const int i) { return rdpmc(i); }
};

struct mck_rawmsr_policy {
  static int init(const int i, const int config) {
    const uint64_t v = (1 << 22) | (1 << 16) | config;
    wrmsr(IA32_PERFEVTSEL_BASE + i, v, {});
    return 0;
  }

  static void reset(const int i) { wrmsr(IA32_PMC_BASE + i, 0, {}); }
  static void start(const unsigned long mask) {
    wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, {});
    wrmsr(IA32_PERF_GLOBAL_CTRL, mask, {});
  }
  static void stop(const unsigned long mask) {
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, {});
    const uint64_t ovf = rdmsr(IA32_PERF_GLOBAL_STATUS, {});
    if (ovf) {
      std::cout << "Overflow: " << std::hex << ovf << std::dec << std::endl;
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, {});
    }
  }
  static uint64_t read(const int i) { return rdmsr(IA32_PMC_BASE + i, {}); }
};

template <typename ACCESS>
class mck_msr {
  gsl::span<uint64_t>::const_iterator buf_;
  uint64_t global_ctrl_;
  int active = 0;
  unsigned long active_mask_ = 0;
public:
/* sufficient to pass vec of perf_event_attr */
  mck_msr(const pmc &pmcs, gsl::span<uint64_t>::const_iterator buf)
      : buf_(buf), global_ctrl_(rdmsr(IA32_PERF_GLOBAL_CTRL, -1)) {
    pmu_info({});
    for (const auto &pmc : pmcs) {
      struct perf_event_attr attr = resolve_pmc(pmc.name());
      const int err = ACCESS::init(active, attr.config);
      if (err) {
        std::cerr << "Error configuring PMU with " << pmc.name() << std::hex
                  << attr.config << std::dec << std::endl;
        continue;
      }
      ++active;
    }

    active_mask_ = static_cast<unsigned long>((1 << active) - 1);
  }
  ~mck_msr() { wrmsr(IA32_PERF_GLOBAL_CTRL, global_ctrl_, {}); }

  void start() {
    for (int i = 0; i < active; ++i) {
      ACCESS::reset(i);
    }
    ACCESS::start(active_mask_);
  }

  void stop() {
    ACCESS::stop(active_mask_);
    for (int i = 0; i < active; ++i) {
      *buf_ = ACCESS::read(i);
      ++buf_;
    }
  }
};


class x86_64_pmu : arch_pmu_base<x86_64_pmu> {
  const pmc &pmcs_;
  size_t iterations_;

  bool is_mckernel;

  std::vector<std::unique_ptr<rdpmc_ctx>> rdpmc_ctxs;
  std::vector<std::unique_ptr<fd>> perf_fds;
  std::unique_ptr<fd> msr_fd;
  uint64_t global_ctrl_;
  int active = 0;

public:
  x86_64_pmu(const pmc &pmcs, const size_t iterations, const unsigned cpu)
      : arch_pmu_base(pmcs.size() * iterations), pmcs_(pmcs),
        iterations_(iterations), is_mckernel(mck_is_mckernel())
#if defined(RAWMSR)
        ,
        msr_fd(std::make_unique<fd>(open(
            ("/dev/cpu/" + std::to_string(cpu) + "/msr").c_str(), O_RDWR)))
#endif
#if defined(MCK_MCK) || defined(RAWMSR)
        ,
        global_ctrl_(rdmsr(IA32_PERF_GLOBAL_CTRL, -1))
#endif
  {
#if defined(RAWMSR)
    if (!msr_fd) {
      throw std::runtime_error("Opening MSR failed: " + std::to_string(errno) +
                               strerror(errno) + '\n');
    }
#endif

    pmu_info(msr_fd);

    for (const auto &pmc : pmcs_) {
      struct perf_event_attr attr;
      int err = resolve_event(pmc.name().c_str(), &attr);
      if (err) {
        std::cout << "Error resolving event: \"" << pmc.name() << "\".\n";
        continue;
      }

      if (is_mckernel) {
#if defined(MCK_MCK)
        int err = mck_pmc_init(active, attr.config, 0x4);
        if (err) {
          std::cerr << "Error configuring PMU with \"" << pmc.name() << "\" "
                    << std::hex << attr.config << std::dec << '\n';
          continue;
        }
#elif defined(MCK_RAWMSR)
        const uint64_t v = (1 << 22) | (1 << 16) | attr.config;
        wrmsr(IA32_PERFEVTSEL_BASE + active, v, msr_fd);
#else
        std::cerr << "PMU method not implmented in McKernel.\n";
#endif
      } else {
#if defined(LINUX_JEVENTS)
        auto ctx = std::make_unique<rdpmc_ctx>();
        err = rdpmc_open_attr(&attr, ctx.get(), NULL);
        if (err) {
          std::cout << "Error opening RDPMC context for event \"" << pmc.name()
                    << "\"\n";
          continue;
        } else {
          rdpmc_ctxs.push_back(std::move(ctx));
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
        try {
          perf_fds.push_back(make_perf_fd(pe));
        } catch (const auto &e) {
          std::cerr << pmc.name() << ": " << e.what() << std::endl;
          continue;
        }
#elif defined(LINUX_RAWMSR)
        const uint64_t v = (1 << 22) | (1 << 16) | attr.config;
        wrmsr(IA32_PERFEVTSEL_BASE + active, v, msr_fd);
#else
        std::cerr << "PMU method not implemented in Linux.\n";
#endif
      }

      ++active;
    }
  }

  ~x86_64_pmu() {
    if (!is_mckernel) {
#if defined(LINUX_RAWMSR)
      for (int i = 0; i < active; ++i) {
        wrmsr(IA32_PERFEVTSEL_BASE + i, 0, msr_fd);
      }

      wrmsr(IA32_PERF_GLOBAL_CTRL, global_ctrl_, msr_fd);
#endif
    } else {
      wrmsr(IA32_PERF_GLOBAL_CTRL, global_ctrl_, msr_fd);
    }
  }

  void begin() {
    if (is_mckernel) {
      for (int i = 0; i < active; ++i) {
#if defined(MCK_MCK)
        mck_pmc_reset(i);
#elif defined(MCK_RAWMSR)
        wrmsr(IA32_PMC_BASE + i, 0, msr_fd);
#endif
      }

#if defined(MCK_RAWMSR)
      const uint64_t mask = (1 << active) - 1;
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, msr_fd);
      wrmsr(IA32_PERF_GLOBAL_CTRL, mask, msr_fd);
#endif
    } else {
#if defined(LINUX_JEVENTS)
      for (auto &&ctx : rdpmc_ctxs) {
        //data[i] = rdpmc_read(&pmus->ctx[i]);
        rdpmc_read(ctx.get());
      }
#elif defined(LINUX_PERF)
      for (const auto &perf_fd : perf_fds) {
        if (ioctl(static_cast<int>(*perf_fd), PERF_EVENT_IOC_RESET, 0)) {
          std::cerr << "Error in ioctl\n";
        }
      }
#endif
      for (unsigned i = 0; i < active; ++i) {
#if defined(LINUX_RAWMSR)
      wrmsr(IA32_PMC_BASE + i, 0, msr_fd);
#endif
      }

#if defined(LINUX_RAWMSR)
      const uint64_t mask = (1 << pmus->active) - 1;
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, pmus->msr_fd);
      wrmsr(IA32_PERF_GLOBAL_CTRL, mask, pmus->msr_fd);
#endif
    }
  }
  void end() {}
};

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

