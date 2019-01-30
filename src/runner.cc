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

benchmark_result::benchmark_result(const hwloc::cpuset &cpus,
                                   const unsigned repetitions,
                                   const bool irreflexive,
                                   const hwloc::cpuset &by)
    : cpus_(std::make_unique<hwloc::cpuset>(cpus)), repetitions_(repetitions),
      data_(cpus_->size() * repetitions_) {
  for (const auto &from : *cpus_) {
    for (const auto &to : *cpus_) {
      if (irreflexive && (from == to)) {
        continue;
      }
      if (!by) {
        column_names_.push_back(std::to_string(from.second) + '-' +
                                std::to_string(to.second));
      } else {
        for (const auto neighbor : by) {
          column_names_.push_back(std::to_string(from.second) + '-' +
                                  std::to_string(to.second) + '.' +
                                  std::to_string(neighbor.second));
          column_names_.push_back(std::to_string(neighbor.second) + '.' +
                                  std::to_string(from.second) + '-' +
                                  std::to_string(to.second));
        }
      }
    }
  }
  slice_length_ = 1;
  data_.resize(column_names_.size() * repetitions_);
}

benchmark_result::benchmark_result(const hwloc::cpuset &cpus, const pmc &pmcs,
                                   const unsigned repetitions)
    : cpus_(std::make_unique<hwloc::cpuset>(cpus)), pmcs_(&pmcs),
      repetitions_(repetitions), slice_length_(pmcs.size()),
      data_(cpus_->size() * pmcs_->size() * repetitions_) {
  for (const auto &cpu : *cpus_) {
    /* timestamp is captured implicitly */
    const auto physical = cpu.second;
    column_names_.push_back("timestamp-c" + std::to_string(physical));
    for (const auto &pmc : pmcs) {
      column_names_.push_back(pmc.name() + "-c" + std::to_string(physical));
    }
  }
}

gsl::span<uint64_t> benchmark_result::buffer_for_thread(const unsigned i) {
  using index_type = gsl::span<uint64_t>::index_type;
  auto span = gsl::span<uint64_t>(data_);
  return span.subspan(gsl::narrow<index_type>(i * repetitions_ * slice_length_),
                      gsl::narrow<index_type>(repetitions_ * slice_length_));
}

std::ostream &operator<<(std::ostream &os, const benchmark_result &result) {
  /* header */
  for (const auto &column_name : result.column_names_) {
    os << std::setw(4) << column_name << ' ';
  }
  os << '\n';

  const auto columns = result.column_names_.size();
  for (unsigned rep = 0; rep < result.repetitions_; ++rep) {
    for (unsigned col = 0; col < columns; ++col) {
      const auto chunk_offset = col / result.slice_length_;
      const auto chunk_idx = col % result.slice_length_;

      const auto col_offset =
          result.repetitions_ * result.slice_length_ * chunk_offset;
      const auto rep_offset = rep * result.slice_length_;
      os << std::setw(5) << result.data_.at(col_offset + rep_offset + chunk_idx)
         << ' ';
    }
    os << '\n';
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
      ret.set(static_cast<unsigned>(cpu));
    }
  }

  return ret;
}

static int current_cpu_getaffinity() {
  const int cpu = sched_getcpu();
  if (cpu < 0) {
    perror("sched_getcpu() failed");
  }

  return cpu;
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
    const int sched_cpu = current_cpu_getaffinity();
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

public:
  static void run_benchmark_external(benchmark_t *ops, const unsigned cpu,
                                     const unsigned reps,
                                     gsl::span<uint64_t> results,
                                     barrier &barrier) {
    auto offset = gsl::span<uint64_t>::index_type{0};
    // set dst cpu via argument?

    for (unsigned rep = 0; rep < reps; ++rep) {
      /* todo accomodate other PMCS */
      auto slice = results.subspan(offset, 1);
      barrier.wait();
      ops->extern_call(nullptr, cpu, slice.data(), slice.size());
      offset += slice.size();
    }
    barrier.wait();
  }

private:
  static void run_benchmark(benchmark_t *ops,
                            const pmc &pmcs, const unsigned reps,
                            gsl::span<uint64_t> results, barrier &barrier) {
    void *benchmark_arg =
        (ops->init_arg) ? ops->init_arg(ops->state) : ops->state;

    auto pmu = pmcs.configure();
    auto offset = gsl::span<uint64_t>::index_type{0};
    const auto size = gsl::narrow<gsl::span<uint64_t>::index_type>(pmcs.size());

    barrier.wait();

    // reps = warm-up + benchmark runs.
    for (unsigned rep = 0; rep < reps; ++rep) {
      if (ops->reset_arg) {
        ops->reset_arg(benchmark_arg);
      }

      auto buf = results.subspan(offset, size);

      pmu->start(buf);
      ops->call(benchmark_arg);
      const auto written = pmu->stop(buf);

      offset += written;
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
  executor(hwloc::topology *topology, hwloc::cpuset cpuset,
           const bool dirigent = false, const bool do_binding = true)
      : dirigent_(dirigent), loop_([=, cpuset = std::move(cpuset)]() mutable {
          thread_fn(*topology, cpuset, dirigent, do_binding);
        }) {
    if (!dirigent) {
      thread_ = std::thread(loop_);
    }
  }

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

  void submit_work(std::packaged_task<void()> task) {
    std::list<std::packaged_task<void()>> tmp;
    tmp.emplace_back(std::move(task));

    {
      std::lock_guard<std::mutex> lock(queue_lock_);
      queue_.splice(queue_.end(), std::move(tmp));
    }
    cv_.notify_one();
  }

  bool dirigent() const { return dirigent_; }
  void run_dirigent() { loop_(); }
};

runner::runner(hwloc::topology *const topology, const hwloc::cpuset &cpuset,
               bool include_hyperthreads, bool do_binding) {
  const hwloc_obj_type_t type =
      (include_hyperthreads) ? HWLOC_OBJ_PU : HWLOC_OBJ_CORE;
  const auto depth = topology->get_type_or_below_depth(type);
  const unsigned have_cores = topology->get_nbobjs(depth);
  const unsigned wanted_cores = cpuset.size();

  std::cerr << "Spawning " << cpuset.size() << " workers: " << cpuset
            << std::endl;

  // iterate over all cores and pick the ones in the cpuset
  for (unsigned pu = 0, i = 0; i < wanted_cores && pu < have_cores; ++pu) {
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
    const unsigned cpunum = static_cast<unsigned>(cpunum_check);

    const bool dirigent = (i == 0);
    threads_.emplace_back(std::make_unique<executor>(topology, std::move(tmp),
                                                     dirigent, do_binding));

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
    benchmark_t null_ops = {"null",  nullptr, nullptr,
                            nullptr, nullptr, [](void *ptr) {
                              return ptr; },
                            nullptr};
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

    std::cerr << "\rRunning " << index + 1 << " of " << cpuset_.size() << '.';
    if (index) {
      const auto h = duration_cast<hours>(diff);
      const auto m = duration_cast<minutes>(diff - h);
      const auto s = duration_cast<seconds>(diff - h - m);
      std::cerr << " Last took ";
      std::cerr << h.count() << ':' << m.count() << ':' << s.count();
      std::cerr << " (" << duration_cast<seconds>(diff).count() << ")";
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

  std::cerr << std::endl;

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

std::unique_ptr<benchmark_result> runner::matrix(benchmark_t *ops,
                                                 const unsigned reps) {
  auto result = std::make_unique<benchmark_result>(cpuset_, reps);

  barrier barrier(1);
  for (const auto &from : cpuset_) {
    for (const auto &to : cpuset_) {
      std::cerr << "\rRunning " << from.first << " to " << to.first << '.'
                << std::flush;
      const auto thread_idx = from.first;
      const auto index = cpuset_.size() * from.first + to.first;
      const auto cpu_idx = from.second;
      const auto &executor = threads_.at(thread_idx);
      const auto target = to.second;

      std::packaged_task<void()> task(
          [ops, cpu_idx, target, reps, results = result->buffer_for_thread(index),
           &barrier] {
            executor::run_benchmark_external(ops, target, reps, results,
                                             barrier);
          });
      auto future = task.get_future();
      executor->submit_work(std::move(task));

      if (executor->dirigent()) { // run dirigent
        assert(thread_idx == 0);
        executor->run_dirigent();
      }
      future.get();
    }
  }

  return result;
}

std::unique_ptr<benchmark_result>
runner::parallel_matrix(benchmark_t *ops1, benchmark_t *ops2,
                        const hwloc::bitmap &cpuset1,
                        const hwloc::cpuset &cpuset2, const unsigned reps) {
  {
    assert(!cpuset1.intersects(cpuset2));

    hwloc::bitmap cpuset = cpuset1 | cpuset2;
    cpuset &= cpuset_;
    assert(cpuset.isincluded(cpuset_));
  }

  const auto size = 2; //cpuset.size();
  assert(size > 0);
  auto result =
      std::make_unique<benchmark_result>(cpuset1, reps, true, cpuset2);

  for (const auto &from : cpuset1) {
    for (const auto &to : cpuset1) {
      if (from == to) {
        continue;
      }
      for (const auto &by : cpuset2) {
        const auto index = from.first * cpuset1.size() * cpuset2.size() * 2 +
                           (to.first - 1) * cpuset2.size() * 2 + by.first * 2;
        const auto target = to.second;

        std::cout << "IPI " << from.second << "->" << to.second << "/"
                  << by.second << " " << index << std::endl;

        std::vector<std::future<void>> futures;
        futures.reserve(size);
        barrier barrier(size);

        {
          const auto thread_idx = from.second;
          const auto &executor = threads_.at(thread_idx);
          std::packaged_task<void()> task(
              [ops1, target, reps, results = result->buffer_for_thread(index),
               &barrier] {
                executor::run_benchmark_external(ops1, target, reps, results,
                                                 barrier);
              });
          auto future = task.get_future();
          executor->submit_work(std::move(task));
          futures.push_back(std::move(future));
        }
        {
          const auto thread_idx = by.second;
          const auto &executor = threads_.at(thread_idx);
          auto future = executor->submit_work(
              ops2, {}, reps, result->buffer_for_thread(index + 1), barrier);
          futures.push_back(std::move(future));
        }

        // run dirigent only if required; else deadlock.
        if (from.second == 0 || by.second == 0) {
          for (auto &&thread : threads_) {
            if (thread->dirigent()) {
              thread->run_dirigent();
              break;
            }
          }
        }
        // wait for all workers to finish
        for (auto &&future : futures) {
          future.get();
        }
      }
    }
  }

  return result;
}

