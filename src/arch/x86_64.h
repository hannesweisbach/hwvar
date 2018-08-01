#pragma once

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

#if 0
struct pmu {
  struct rdpmc_ctx *ctx;
  int *perf_fds;
  unsigned active;
  int mckernel;
  char *msr;
  int msr_fd;
  uint64_t global_ctrl;
};
#endif
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

#if 0
class events {
  std::vector<event> events_;

public:
  events(const pmc &pmcs) {
    for (const auto &pmc : pmcs) {
      try {
        events_.emplace_back(pmc.name());
      } catch (...) {
      }
    }
  }
  auto begin() const { return events_.begin(); }
  auto end() const { return events_.end(); }
  auto size() const { return events_.size(); }
};
#endif

struct x86_pmc_base {
  virtual ~x86_pmc_base() = default;
  virtual void start(gsl::span<uint64_t>::iterator) = 0;
  virtual void stop(gsl::span<uint64_t>::iterator) = 0;
};

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

class msr_mck {
public:
  msr_mck() {}
  uint64_t rdmsr(const uint32_t reg) const { return syscall(850, reg); }
  uint64_t wrmsr(const uint32_t reg, const uint64_t val) const {
    return syscall(851, reg, val);
  }
};

class msr_linux {
  std::unique_ptr<fd> fd_;

  static unsigned cpu() {
    int cpu = sched_getcpu();
    if (cpu < 0) {
      using namespace std::string_literals;
      throw std::runtime_error("Error getting CPU number: "s + strerror(errno) +
                               " (" + std::to_string(errno) + ")\n");
    }
    /* TODO: check thread affinity mask
     * - has a single CPU
     * - this single CPU is this CPU
     */
    return cpu;
  }

public:
  msr_linux()
      : fd_(std::make_unique<fd>(open(
            ("/dev/cpu/" + std::to_string(cpu()) + "/msr").c_str(), O_RDWR))) {}

  uint64_t rdmsr(const uint32_t reg) {
    uint64_t v;
    const ssize_t ret = pread(static_cast<int>(*fd_), &v, sizeof(v), reg);
    if (ret != sizeof(v)) {
      std::cerr << "Reading MSR " << std::hex << reg << std::dec << " failed."
                << std::endl;
    }
    return v;
  }

  uint64_t wrmsr(const uint32_t reg, const uint64_t val) {
    const ssize_t ret = pwrite(static_cast<int>(*fd_), &val, sizeof(val), reg);
    if (ret != sizeof(val)) {
      std::cerr << "Writing MSR " << std::hex << reg << std::dec << " failed."
                << std::endl;
    }
    return ret;
  }
};

template <typename MSR>
class rawmsr_policy : MSR {
public:
  int init(const int i, const int config) {
    const uint64_t v = (1 << 22) | (1 << 16) | config;
    wrmsr(IA32_PERFEVTSEL_BASE + i, v, {});
    return 0;
  }

  void reset(const int i) { wrmsr(IA32_PMC_BASE + i, 0, {}); }
  void start(const unsigned long mask) {
    wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, {});
    wrmsr(IA32_PERF_GLOBAL_CTRL, mask, {});
  }
  void stop(const unsigned long mask) {
    wrmsr(IA32_PERF_GLOBAL_CTRL, 0, {});
    const uint64_t ovf = rdmsr(IA32_PERF_GLOBAL_STATUS, {});
    if (ovf) {
      std::cout << "Overflow: " << std::hex << ovf << std::dec << std::endl;
      wrmsr(IA32_PERF_GLOBAL_OVF_CTRL, 0, {});
    }
  }
  /* Should be equivalent to rdpmc instruction */
  uint64_t read(const int i) { return rdmsr(IA32_PMC_BASE + i, {}); }
};

template <typename ACCESS>
class msr_pmc : ACCESS, public x86_pmc_base {
  uint64_t global_ctrl_;
  int active = 0;
  unsigned long active_mask_ = 0;
public:
  /* sufficient to pass vec of perf_event_attr */
  msr_pmc(const pmc &pmcs)
      : global_ctrl_(rdmsr(IA32_PERF_GLOBAL_CTRL, -1)) {
    pmu_info({});
    for (const auto &pmc : pmcs) {
      const int err = ACCESS::init(active, pmc.data().attr().config);
      if (err) {
        std::cerr << "Error configuring PMU " << pmc.name() << " with "
                  << std::hex << pmc.data().attr().config << std::dec
                  << std::endl;
        continue;
      }
      ++active;
    }

    active_mask_ = static_cast<unsigned long>((1 << active) - 1);
  }
  ~msr_pmc() { wrmsr(IA32_PERF_GLOBAL_CTRL, global_ctrl_, {}); }

  void start(gsl::span<uint64_t>::iterator) override {
    for (int i = 0; i < active; ++i) {
      ACCESS::reset(i);
    }
    ACCESS::start(active_mask_);
  }

  void stop(gsl::span<uint64_t>::iterator buf) override {
    ACCESS::stop(active_mask_);
    for (int i = 0; i < active; ++i) {
      *buf = ACCESS::read(i);
      ++buf;
    }
  }
};

using mck_rawmsr = msr_pmc<rawmsr_policy<msr_mck>>;
using mck_mckmsr = msr_pmc<mck_mck_policy>;
using linux_rawmsr = msr_pmc<rawmsr_policy<msr_linux>>;

class linux_jevents : public x86_pmc_base {
  struct rdpmc_t {
    struct rdpmc_ctx ctx;
    int close = 0;

    rdpmc_t(struct perf_event_attr attr) {
      const int err = rdpmc_open_attr(&attr, &ctx, NULL);
      if (err) {
        throw std::runtime_error(std::to_string(errno) + ' ' + strerror(errno));
      }

      close = 1;
    }
    ~rdpmc_t() {
      if (close) {
        rdpmc_close(&ctx);
      }
    }
    rdpmc_t(const rdpmc_t &) = delete;
    rdpmc_t(rdpmc_t &&rhs) {
      this->ctx = rhs.ctx;
      this->close = rhs.close;
      rhs.close = 0;
    }

    uint64_t read() { return rdpmc_read(&ctx); }
  };

  std::vector<rdpmc_t> rdpmc_ctxs;

public:
  linux_jevents(const pmc &pmcs) {
    for (const auto &pmc : pmcs) {
      rdpmc_ctxs.emplace_back(pmc.data().attr());
    }
  }

  void start(gsl::span<uint64_t>::iterator buf) override {
    for (auto &&pmc : rdpmc_ctxs) {
      *buf = pmc.read();
      ++buf;
    }
  }

  void stop(gsl::span<uint64_t>::iterator buf) override {
    for (auto &&pmc : rdpmc_ctxs) {
      *buf = pmc.read() - *buf;
      ++buf;
    }
  }
};

class linux_perf : public x86_pmc_base {
  std::vector<std::unique_ptr<fd>> perf_fds;

public:
  linux_perf(const pmc &pmcs) {
    for (const auto &pmc : pmcs) {
      struct perf_event_attr pe;
      memset(&pe, 0, sizeof(pe));
      const auto &attr = pmc.data().attr();
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
      } catch (const std::exception &e) {
        std::cerr << pmc.name() << ": " << e.what() << std::endl;
      }
    }
  }

  void start(gsl::span<uint64_t>::iterator) override {
    for (const auto &perf_fd : perf_fds) {
      if (ioctl(static_cast<int>(*perf_fd), PERF_EVENT_IOC_RESET, 0)) {
        std::cerr << "Error in ioctl\n";
      }
    }
  }

  void stop(gsl::span<uint64_t>::iterator buf) override {
    long long v;
    for (const auto &perf_fd : perf_fds) {
      const auto ret = read(static_cast<int>(*perf_fd), &v, sizeof(v));
      if (ret < 0) {
        std::cerr << "perf: Error reading performance counter: " << errno
                  << ": " << strerror(errno) << std::endl;
      } else if (ret != sizeof(v)) {
        std::cerr << "perf: Error reading " << sizeof(v) << " bytes."
                  << std::endl;
      }
      *buf = v;
      ++buf;
    }
  }
};


template <typename MCK, typename LINUX> class x86_pmu {
  std::unique_ptr<x86_pmc_base> backend_;

public:
  x86_pmu(const pmc &pmcs)
      : backend_(mck_is_mckernel() ? static_cast<std::unique_ptr<x86_pmc_base>>(
                                         std::make_unique<MCK>(pmcs))
                                   : static_cast<std::unique_ptr<x86_pmc_base>>(
                                         std::make_unique<LINUX>(pmcs))) {}

  void start(gsl::span<uint64_t>::iterator it) { backend_->start(it); }
  void stop(gsl::span<uint64_t>::iterator it) { backend_->stop(it); }

  uint64_t timestamp_begin() {
    unsigned high, low;
    __asm__ volatile("CPUID\n\t"
                     "RDTSC\n\t"
                     "mov %%edx, %0\n\t"
                     "mov %%eax, %1\n\t"
                     : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");

    return (uint64_t)high << 32ULL | low;
  }

  uint64_t timestamp_end() {
    unsigned high, low;
    __asm__ volatile("RDTSCP\n\t"
                     "mov %%edx,%0\n\t"
                     "mov %%eax,%1\n\t"
                     "CPUID\n\t"
                     : "=r"(high), "=r"(low)::"%rax", "%rbx", "%rcx", "%rdx");
    return (uint64_t)high << 32ULL | low;
  }
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

