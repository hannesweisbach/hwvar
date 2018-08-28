#include <chrono>
#include <condition_variable>
#include <iomanip>
#include <iostream>
#include <list>
#include <mutex>
#include <thread>
#include <future>

#include <upca/upca.h>

#include <config.h>

#include "hwloc"
#include "runner.h"
#include <barrier.h>
#include <benchmark.h>
#include <mckernel.h>

benchmark_result::benchmark_result(const hwloc::cpuset &cpus, const pmc &pmcs,
                                   const unsigned repetitions)
    : cpus_(std::make_unique<hwloc::cpuset>(cpus)), pmcs_(&pmcs),
      repetitions_(repetitions),
      data_(cpus_->size() * pmcs_->size() * repetitions_) {}

gsl::span<uint64_t> benchmark_result::buffer_for_thread(const int i) {
  auto span = gsl::span<uint64_t>(data_);
  return span.subspan(i * repetitions_ * pmcs_->size(),
                      repetitions_ * pmcs_->size());
}

std::ostream &operator<<(std::ostream &os, const benchmark_result &result) {
  for (const auto &pmc : *result.pmcs_) {
    os << "# " << pmc.name() << '\n';
    for (const auto cpu : *result.cpus_) {
      const auto logical = cpu.first;
      const auto physical = cpu.second;
      os << std::setw(3) << physical << ' ';
      for (unsigned rep = 0; rep < result.repetitions_; ++rep) {
        os << std::setw(10)
           << result
                  .data_[logical * result.repetitions_ * result.pmcs_->size() +
                         result.pmcs_->size() * rep + pmc.offset()]
           << ' ';
      }
      os << '\n';
    }
  }

  return os;
}

#ifdef HAVE_SCHED_H
static hwloc::cpuset current_cpuset_getaffinity() {
  hwloc::cpuset ret;
  cpu_set_t cpuset;

  if (sched_getaffinity(0, sizeof(cpuset), &cpuset)) {
    perror("sched_getaffinity() failed");
  }

  for (int cpu = 0; cpu < CPU_SETSIZE; ++cpu) {
    if (CPU_ISSET(cpu, &cpuset)) {
      ret.set((unsigned)cpu);
    }
  }

  return ret;
}

static unsigned current_cpu_getaffinity() {
  const int cpu = sched_getcpu();
  if (cpu < 0) {
    perror("sched_getcpu() failed");
  }

  return (unsigned)cpu;
}
#endif

/* binds the calling thread to the cpuset */
static void bind_thread(hwloc::topology &topology,
                        const hwloc::cpuset &cpuset, const bool do_binding) {
  if (do_binding) {
    if (topology.is_thissystem()) {
      try {
        topology.set_cpubind(cpuset, HWLOC_CPUBIND_THREAD);
      } catch (const std::runtime_error &e) {
        std::cerr << e.what() << std::endl;
      }
    } else {
#ifdef HAVE_SCHED_H
      cpu_set_t cpuset_;
      CPU_ZERO(&cpuset_);
      CPU_SET(cpuset.first(), &cpuset_);
      if (sched_setaffinity(0, sizeof(cpuset_), &cpuset_)) {
        perror("sched_setaffinity() failed");
      }
#endif
    }

    const int cpu = cpuset.first();
    const hwloc::cpuset hwloc_cpuset = topology.get_cpubind();
    const int hwloc_cpu = topology.get_last_cpu_location().first();

#ifdef HAVE_SCHED_H
    hwloc::bitmap sched_cpuset = current_cpuset_getaffinity();
    const unsigned sched_cpu = current_cpu_getaffinity();
#endif

    const bool equal = cpuset.isequal(hwloc_cpuset)
                       /* Unfortunately hwloc_get_last_cpu_location() does not
                          (yet?) work under McKernel. */
                       && (mck_is_mckernel() || (cpu == hwloc_cpu))
#ifdef HAVE_SCHED_H
                       && cpuset.isequal(sched_cpuset) && (cpu == sched_cpu)
#endif
        ;

    if (!equal) {
      std::cerr << "Target: " << cpuset << '/' << cpu;
#ifdef HAVE_SCHED_H
      std::cerr << ", getaffinity/cpu(): " << sched_cpuset << '/' << sched_cpu;
#endif
      std::cerr << ", hwloc: " << hwloc_cpuset << '/' << hwloc_cpu << std::endl;
    } else {
      std::cerr << "Thread bound to " << cpuset << '/' << cpu << '\n';
    }
  } else {
    /* If there's no binding at least record the current CPU number */
#ifdef HAVE_SCHED_H
    //cpu = current_cpu_getaffinity();
#endif
  }
}

class runner::executor{
  mutable std::mutex queue_lock_;
  mutable std::condition_variable cv_;
  mutable std::list<std::packaged_task<void()>> queue_;

  bool run_ = true;
  bool initialized_ = false;
  bool dirigent_;

  std::function<void()> loop_;
  std::thread thread_;

  static void run_benchmark(benchmark_t *ops, const pmc &pmcs,
                            const unsigned reps, gsl::span<uint64_t> results,
                            barrier &barrier) {
    void *benchmark_arg =
        (ops->init_arg) ? ops->init_arg(ops->state) : ops->state;

    auto pmu = pmcs.configure(results);

    barrier.wait();

    // reps = warm-up + benchmark runs.
    for (unsigned rep = 0; rep < reps; ++rep) {
      if (ops->reset_arg) {
        ops->reset_arg(benchmark_arg);
      }

      pmu.start();
      ops->call(benchmark_arg);
      pmu.stop();
    }

    barrier.wait();
  }

  void thread_fn(hwloc::topology &topology, const hwloc::cpuset &cpuset,
                 const bool dirigent, const bool do_binding) {
    if (do_binding && !initialized_) {
      bind_thread(topology, cpuset, do_binding);
      initialized_ = true;
    }

    while (run_) {
      std::list<std::packaged_task<void()>> tmp;

      {
        std::unique_lock<std::mutex> lock(queue_lock_);
        cv_.wait(lock, [&] { return !queue_.empty(); });

        tmp.splice(tmp.begin(), queue_, queue_.begin());
      }

      for (auto &&task : tmp) {
        task();
      }

      if (dirigent)
        break;
    }

    if (!dirigent)
      std::cerr << "Thread " << cpuset << " stopped.\n";
  }

public:
  executor(hwloc::topology *topology, const unsigned thread_num,
           const unsigned cpunum, const hwloc::cpuset cpuset,
           const bool dirigent = false, const bool do_binding = true)
      : dirigent_(dirigent), loop_([=]() mutable {
          thread_fn(*topology, cpuset, dirigent, do_binding);
        }) {
    if (!dirigent) {
      thread_ = std::thread(loop_);
    }
  };

  ~executor() {
    if (!dirigent_) {
      std::list<std::packaged_task<void()>> tmp;
      tmp.emplace_back([this] { run_ = false; });
      auto future = tmp.front().get_future();

      {
        std::lock_guard<std::mutex> lock(queue_lock_);
        queue_.splice(queue_.end(), std::move(tmp));
      }
      cv_.notify_one();

      future.get();
      thread_.join();
    }
  }

  std::future<void> submit_work(benchmark_t *ops, const pmc &pmcs,
                                const unsigned reps,
                                gsl::span<uint64_t> results,
                                barrier &barrier) const {
    std::list<std::packaged_task<void()>> tmp;
    tmp.emplace_back([ops, &pmcs, reps, results, &barrier] {
      run_benchmark(ops, pmcs, reps, results, barrier);
    });

    auto future = tmp.front().get_future();

    {
      std::lock_guard<std::mutex> lock(queue_lock_);
      queue_.splice(queue_.end(), std::move(tmp));
    }
    cv_.notify_one();

    return future;
  }

  bool dirigent() const { return dirigent_; }
  void run_dirigent() { loop_(); }
};

runner::runner(hwloc::topology *const topology, const hwloc::cpuset &cpuset,
               bool include_hyperthreads, bool do_binding) {
  const hwloc_obj_type_t type =
      (include_hyperthreads) ? HWLOC_OBJ_PU : HWLOC_OBJ_CORE;
  const auto depth = topology->get_type_or_below_depth(type);
  const unsigned num_threads = topology->get_nbobjs(depth);

  std::cerr << "Spawning " << cpuset.size() << " workers: " << cpuset
            << std::endl;

  // iterate over all cores and pick the ones in the cpuset
  for (unsigned pu = 0, i = 0; pu < num_threads; ++pu) {
    /* TODO Awesome:
     * If no object for that type exists, NULL is returned. If there
     * are several levels with objects of that type, NULL is returned and ther
     * caller may fallback to hwloc_get_obj_by_depth(). */
    hwloc_obj_t obj = topology->get_obj(type, pu);
    if (obj == nullptr) {
      throw std::runtime_error("Error getting obj. Implement fallback to "
                               "hwloc_get_obj_by_depth()?\n");
    }

    hwloc::cpuset tmp(obj->cpuset);
    tmp.singlify();
    // TODO might this get the same mask for two sets?!
    if (!tmp.isincluded(cpuset)) {
      continue;
    }
    const int cpunum_check = tmp.first();
    if (cpunum_check < 0) {
      throw std::runtime_error("No index is set in the bitmask\n");
    }
    const unsigned cpunum = (unsigned)cpunum_check;

    const bool dirigent = (i == 0);
    threads_.emplace_back(std::make_unique<executor>(topology, i, cpunum, tmp, dirigent, do_binding));

#if 0
    fprintf(stderr, "Found L:%u P:%u %s %d %d %d\n", obj->logical_index,
            obj->os_index, thread->thread_arg.cpuset_string, i, pu,
            cpunum);
#endif

    cpuset_.set(cpunum);

    i = i + 1;
  }

  assert(cpuset_.size() == cpuset.size());
  /* wait for all threads to initialize */
  {
    benchmark_t null_ops = {
        "null", NULL, NULL, NULL, NULL, [](void *ptr) {
                              return ptr; }, NULL};
    parallel(&null_ops, 1, pmc{});
  }
}

runner::~runner() = default;

std::unique_ptr<benchmark_result> runner::serial(benchmark_t *ops, const unsigned reps,
                                         const pmc &pmcs) {
  using namespace std::chrono;
  auto result = std::make_unique<benchmark_result>(cpuset_, pmcs, reps);

  barrier barrier(1);

  microseconds diff;
  for (const auto &cpu : cpuset_) {
    const auto index = cpu.first;
    const auto &executor = threads_.at(index);

    std::cerr << "Running " << index + 1 << " of " << cpuset_.size() << '.';
    if (index) {
      const auto h = duration_cast<hours>(diff);
      const auto m = duration_cast<minutes>(diff - h);
      const auto s = duration_cast<seconds>(diff - h - m);
      const auto us = diff - h - m - s;
      std::cerr << "Last took ";
      std::cerr << h.count() << ':' << m.count() << ':' << s.count();
      std::cerr << " (" << duration_cast<seconds>(diff).count() << ")\r";
    }
    std::cerr << std::flush;

    const auto start = std::chrono::high_resolution_clock::now();
    auto future = executor->submit_work(
        ops, pmcs, reps, result->buffer_for_thread(index), barrier);
    if (executor->dirigent()) { // run dirigent
      assert(index == 0);
      executor->run_dirigent();
    }
    future.get();
    diff = duration_cast<microseconds>(high_resolution_clock::now() - start);
  }

  return result;
}

std::unique_ptr<benchmark_result> runner::parallel(benchmark_t *ops,
                                                   const unsigned reps,
                                                   const pmc &pmcs) {
  auto result = std::make_unique<benchmark_result>(cpuset_, pmcs, reps);
  const auto size = cpuset_.size();
  std::vector<std::future<void>> futures;
  futures.reserve(size);
  barrier barrier(size);

  for (const auto &cpu : cpuset_) {
    const auto index = cpu.first;
    const auto &executor = threads_.at(index);
    auto future = executor->submit_work(
        ops, pmcs, reps, result->buffer_for_thread(index), barrier);
    futures.push_back(std::move(future));
  }

  // run dirigent
  for (auto &&thread : threads_) {
    if (thread->dirigent()) {
      thread->run_dirigent();
      break;
    }
  }

  // wait for all workers to finish
  for (auto &&future : futures) {
    future.get();
  }

  return result;
}

std::unique_ptr<benchmark_result>
runner::parallel(benchmark_t *ops1, benchmark_t *ops2,
                 const hwloc::bitmap &cpuset1, const hwloc::cpuset &cpuset2,
                 const unsigned reps, const pmc &pmcs) {
  assert(!cpuset1.intersects(cpuset2));

  using namespace std::chrono;

  hwloc::bitmap cpuset = cpuset1 | cpuset2;
  cpuset &= cpuset_;
  assert(cpuset.isincluded(cpuset_));

  const auto size = cpuset.size();
  assert(size > 0);
  auto result = std::make_unique<benchmark_result>(cpuset, pmcs, reps);
  barrier barrier(size);

  std::vector<std::future<void>> futures;
  futures.reserve(size);

  for (const auto &cpu : cpuset1) {
    const auto index = cpu.first;
    const auto &executor = threads_.at(index);
    auto future = executor->submit_work(
        ops1, pmcs, reps, result->buffer_for_thread(index), barrier);
    futures.push_back(std::move(future));
  }

  const auto offset = cpuset1.size();
  for (const auto &cpu : cpuset2) {
    const auto index = cpu.first + offset;
    const auto &executor = threads_.at(index);
    auto future = executor->submit_work(
        ops2, pmcs, reps, result->buffer_for_thread(index), barrier);
    futures.push_back(std::move(future));
  }

  // run dirigent
  for (auto &&thread : threads_) {
    if (thread->dirigent()) {
      thread->run_dirigent();
      break;
    }
  }

  // wait for all workers to finish
  for (auto &&future : futures) {
    future.get();
  }

  return result;
}
